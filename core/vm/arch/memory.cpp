/***************************************************************************
* VM memory manager. Implements a "mark and sweep" collection algorithm.
*
* Copyright (c) 2008-2021, Randy Hollines
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

unordered_set<StackFrame**> MemoryManager::pda_frames;
unordered_set<StackFrameMonitor*> MemoryManager::pda_monitors;
vector<StackFrame*> MemoryManager::jit_frames;
set<size_t*> MemoryManager::allocated_memory;

unordered_map<size_t, list<size_t*>*> MemoryManager::free_memory_cache;
size_t MemoryManager::free_memory_cache_size;
unordered_map<size_t, StackMethod*> MemoryManager::virtual_method_table;

bool MemoryManager::initialized;
size_t MemoryManager::allocation_size;
size_t MemoryManager::mem_max_size;
size_t MemoryManager::uncollected_count;
size_t MemoryManager::collected_count;

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
CRITICAL_SECTION MemoryManager::virtual_method_lock;
#else
pthread_mutex_t MemoryManager::pda_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::pda_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::jit_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::allocated_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::marked_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::marked_sweep_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::free_memory_cache_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::virtual_method_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

void MemoryManager::Initialize(StackProgram* p)
{
  prgm = p;
  allocation_size = 0;
  mem_max_size = MEM_MAX;
  uncollected_count = 0;
  free_memory_cache_size = 0;

#ifdef _MEM_LOGGING
  mem_logger.open("mem_log.csv");
  mem_logger << L"cycle,oper,type,addr,size" << endl;
#endif

#ifdef _WIN32
  InitializeCriticalSection(&jit_frame_lock);
  InitializeCriticalSection(&pda_frame_lock);
  InitializeCriticalSection(&pda_monitor_lock);
  InitializeCriticalSection(&allocated_lock);
  InitializeCriticalSection(&marked_lock);
  InitializeCriticalSection(&marked_sweep_lock);
  InitializeCriticalSection(&free_memory_cache_lock);
  InitializeCriticalSection(&virtual_method_lock);
#endif

  initialized = true;
}

// if return true, trace memory otherwise do not
inline bool MemoryManager::MarkMemory(size_t* mem)
{
  if(mem) {
    // check if memory has been marked
    if(mem[MARKED_FLAG]) {
      return false;
    }

    // mark & add to list
#ifndef _GC_SERIAL
    MUTEX_LOCK(&marked_lock);
#endif
    mem[MARKED_FLAG] = 1L;
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&marked_lock);
#endif

    return true;
  }
  
  return false;
}

// if return true, trace memory otherwise do not
inline bool MemoryManager::MarkValidMemory(size_t* mem)
{
  if(mem) {
    // check if memory has been marked
    if(mem[MARKED_FLAG]) {
      return false;
    }

    // mark & add to list
#ifndef _GC_SERIAL
    MUTEX_LOCK(&marked_lock);
#endif
    mem[MARKED_FLAG] = 1L;
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
  wcout << L"adding PDA frame: addr=" << frame << endl;
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
  wcout << L"removing PDA frame: addr=" << frame << endl;
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
  wcout << L"adding PDA method: monitor=" << monitor << endl;
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
  wcout << L"removing PDA method: monitor=" << monitor << endl;
#endif

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_monitor_lock);
#endif
  pda_monitors.erase(monitor);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_monitor_lock);
#endif
}

size_t* MemoryManager::AllocateObject(const long obj_id, size_t* op_stack, long stack_pos, bool collect)
{
  StackClass* cls = prgm->GetClass(obj_id);
#ifdef _DEBUG_GC
  assert(cls);
#endif

  size_t* mem = nullptr;
  if(cls) {
    long size = cls->GetInstanceMemorySize();
#if defined(_WIN64) || defined(_X64) || defined(_ARM64)
    // TODO: memory size is doubled the compiler assumes that integers are 4-bytes.
    // In 64-bit mode integers and floats are 8-bytes.  This approach allocates more
    // memory for floats (a.k.a double) than needed.
    size *= 2;
#endif

    // collect memory
    if(collect && allocation_size + size > mem_max_size) {
      CollectAllMemory(op_stack, stack_pos);
    }

    // allocate memory
#ifdef _DEBUG_GC
    bool is_cached = false;
#endif
    const size_t alloc_size = size * 2 + sizeof(size_t) * EXTRA_BUF_SIZE;
    
    mem = GetMemory(alloc_size);
    mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
    mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
    mem += EXTRA_BUF_SIZE;

    // record
 #ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
 #endif
    allocation_size += size;
    allocated_memory.insert(mem);
 #ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
 #endif

#ifdef _MEM_LOGGING
    mem_logger << mem_cycle << L",alloc,obj," << mem << L"," << size << endl;
#endif

#ifdef _DEBUG_GC
    wcout << L"# allocating object: cached=" << (is_cached ? L"true" : L"false")  << L", addr=" << mem << L"(" 
          << (size_t)mem << L"), size=" << size << L" byte(s), used=" << allocation_size << L" byte(s) #"
          << endl;
#endif
  }

  return mem;
}

size_t* MemoryManager::AllocateArray(const long size, const MemoryType type, size_t* op_stack, long stack_pos, bool collect)
{
  if (size < 0) {
    wcerr << L">>> Invalid allocation size: " << size << L" <<<" << endl;
    exit(1);
  }

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
    wcerr << L">>> Invalid memory allocation <<<" << endl;
    exit(1);
  }

  // collect memory
  if (collect && allocation_size + calc_size > mem_max_size) {
    CollectAllMemory(op_stack, stack_pos);
  }

  // allocate memory
#ifdef _DEBUG_GC
  bool is_cached = false;
#endif
  const size_t alloc_size = calc_size + sizeof(size_t) * EXTRA_BUF_SIZE;

  mem = GetMemory(alloc_size);
  mem[EXTRA_BUF_SIZE + TYPE] = type;
  mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = calc_size;
  mem += EXTRA_BUF_SIZE;

#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
#endif
  allocation_size += calc_size;
  allocated_memory.insert(mem);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _MEM_LOGGING
  mem_logger << mem_cycle << L",alloc,array," << mem << L"," << size << endl;
#endif

#ifdef _DEBUG_GC
  wcout << L"# allocating array: cached=" << (is_cached ? L"true" : L"false") << L", addr=" << mem
    << L"(" << (size_t)mem << L"), size=" << calc_size << L" byte(s), used=" << allocation_size
    << L" byte(s) #" << endl;
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
#ifdef _DEBUG_GC
  wcout << L"*** Raw allocation: address=" << raw_mem << L" ***" << endl;
#endif
  raw_mem[0] = size;
  return raw_mem + 1;
}

void MemoryManager::AddFreeMemory(size_t* raw_mem) {
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
    raw_mem = nullptr;
  }
}

void MemoryManager::AddFreeCache(size_t pool, size_t* raw_mem) {
#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  const size_t mem_size = raw_mem[0];
  free_memory_cache_size += mem_size;

  unordered_map<size_t, list<size_t*>*>::iterator result = free_memory_cache.find(pool);
  if(result == free_memory_cache.end()) {
    list<size_t*>* pool_list = new list<size_t*>;
    pool_list->push_front(raw_mem);
    free_memory_cache.insert(pair<size_t, list<size_t*>*>(pool, pool_list));
  }
  else {
    result->second->push_front(raw_mem);
  }
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&free_memory_cache_lock);
#endif
}

size_t* MemoryManager::GetFreeMemory(size_t size) {
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
    return nullptr;
  }

#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  unordered_map<size_t, list<size_t*>*>::iterator result = free_memory_cache.find(cache_size);
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

void MemoryManager::ClearFreeMemory(bool all) {
#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  unordered_map<size_t, list<size_t*>*>::iterator iter = free_memory_cache.begin();
  for(; iter != free_memory_cache.end(); ++iter) {
    list<size_t*>* free_cache = iter->second;

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

      free_memory_cache.clear();
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
  if (id < 0) {
    return nullptr;
  }

  // upcast
  long virtual_cls_id = id;
  while (virtual_cls_id != -1) {
    if (virtual_cls_id == to_id) {
      return mem;
    }
    // update
    virtual_cls_id = cls_hierarchy[virtual_cls_id];
  }

  // check interfaces
  virtual_cls_id = id;
  while (virtual_cls_id != -1) {
    long* interfaces = cls_interfaces[virtual_cls_id];
    if(interfaces) {
      int i = 0;
      long inf_id = interfaces[i];
      while (inf_id > -1) {
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

void MemoryManager::CollectAllMemory(size_t* op_stack, long stack_pos)
{
#ifdef _TIMING
  wcout << L"=========================================" << endl;
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

#ifndef _GC_SERIAL
#ifdef _WIN32
  HANDLE collect_thread_id = (HANDLE)_beginthreadex(nullptr, 0, CollectMemory, info, 0, nullptr);
  if(!collect_thread_id) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
  
  pthread_t collect_thread;
  if(pthread_create(&collect_thread, &attrs, CollectMemory, (void*)info)) {
    cerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
#endif
#else
  CollectMemory(info);
#endif

#ifndef _GC_SERIAL
#ifdef _WIN32
  if(WaitForSingleObject(collect_thread_id, INFINITE) != WAIT_OBJECT_0) {
    wcerr << L"Unable to join garbage collection threads!" << endl;
    exit(-1);
  }  
  CloseHandle(collect_thread_id);
#else
  void* status;
  if(pthread_join(collect_thread, &status)) {
    cerr << L"Unable to join garbage collection threads!" << endl;
    exit(-1);
  }
  pthread_attr_destroy(&attrs);
#endif  
  MUTEX_UNLOCK(&marked_sweep_lock);
#endif

#ifdef _TIMING
  clock_t end = clock();
  wcout << L"Collection: size=" << mem_max_size << L", time=" << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << endl;
  wcout << L"=========================================" << endl << endl;
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

#ifdef _DEBUG_GC
  size_t start = allocation_size;
  wcout << dec << endl << L"=========================================" << endl;
#ifdef _WIN32  
  wcout << L"Starting Garbage Collection; thread=" << GetCurrentThread() << endl;
#else
  wcout << L"Starting Garbage Collection; thread=" << pthread_self() << endl;
#endif  
  wcout << L"=========================================" << endl;
  wcout << L"## Marking memory ##" << endl;
#endif

#ifndef _GC_SERIAL
#ifdef _WIN32
  const int num_threads = 3;
  HANDLE thread_ids[num_threads];

  thread_ids[0] = (HANDLE)_beginthreadex(nullptr, 0, CheckStatic, info, 0, nullptr);
  if(!thread_ids[0]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[1] = (HANDLE)_beginthreadex(nullptr, 0, CheckStack, info, 0, nullptr);
  if(!thread_ids[1]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  thread_ids[2] = (HANDLE)_beginthreadex(nullptr, 0, CheckPdaRoots, nullptr, 0, nullptr);
  if(!thread_ids[2]) {
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  // join all mark threads
  if(WaitForMultipleObjects(num_threads, thread_ids, TRUE, INFINITE) != WAIT_OBJECT_0) {
    wcerr << L"Unable to join garbage collection threads!" << endl;
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
    cerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  pthread_t stack_thread;
  if(pthread_create(&stack_thread, &attrs, CheckStack, (void*)info)) {
    cerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }

  pthread_t pda_thread;
  if(pthread_create(&pda_thread, &attrs, CheckPdaRoots, nullptr)) {
    cerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
  
  pthread_attr_destroy(&attrs);
  
  // join all of the mark threads
  void *status;

  if(pthread_join(static_thread, &status)) {
    cerr << L"Unable to join garbage collection threads!" << endl;
    exit(-1);
  }
  
  if(pthread_join(stack_thread, &status)) {
    cerr << L"Unable to join garbage collection threads!" << endl;
    exit(-1);
  }

  if(pthread_join(pda_thread, &status)) {
    cerr << L"Unable to join garbage collection threads!" << endl;
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
  wcout << dec << L"Mark time: " << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << endl;
  start = clock();
#endif
  
  // sweep memory
#ifdef _DEBUG_GC
  wcout << L"## Sweeping memory ##" << endl;
#endif

  // sort and search
#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
  MUTEX_LOCK(&marked_lock);
#endif

#ifdef _DEBUG_GC
  wcout << L"-----------------------------------------" << endl;
  wcout << L"Sweeping..." << endl;
  wcout << L"-----------------------------------------" << endl;
#endif

#ifndef _GC_SERIAL

#endif
  set<size_t*> live_memory;

  // for(size_t i = 0; i < allocated_memory.size(); ++i) {
  for (set<size_t*>::iterator iter = allocated_memory.begin(); iter != allocated_memory.end(); ++iter) {
    // size_t* mem = allocated_memory[i];
    size_t* mem = *iter;

    // check dynamic memory
    bool found = false;
    if(mem[MARKED_FLAG]) {
      mem[MARKED_FLAG] = 0L;
      found = true;
    }

    // live
    if(found) {
      live_memory.insert(mem);
    }
    // will be collected
    else {
      // object or array  
      size_t mem_size;
      if(mem[TYPE] == NIL_TYPE) {
        StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
#ifdef _DEBUG_GC
        assert(cls);
#endif
        if(cls) {
#if defined(_WIN64) || defined(_X64) || defined(_ARM64)
          mem_size = cls->GetInstanceMemorySize() * 2;
#else
          mem_size = cls->GetInstanceMemorySize();
#endif
        }
        else {
          mem_size = mem[SIZE_OR_CLS];
        }
      } 
      else {
        mem_size = mem[SIZE_OR_CLS];
      }

      // account for deallocated memory
      allocation_size -= mem_size;

#ifdef _MEM_LOGGING
      mem_logger << mem_cycle << L", dealloc," << (mem[SIZE_OR_CLS] ? "obj," : "array,") << mem << L"," << mem_size << endl;
#endif

      // cache or free memory
      size_t* tmp = mem - EXTRA_BUF_SIZE;
      AddFreeMemory(tmp - 1);
#ifdef _DEBUG_GC
      wcout << L"# freeing memory: addr=" << mem << L"(" << (size_t)mem
            << L"), size=" << mem_size << L" byte(s) #" << endl;
#endif
    }
  }

#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&marked_lock);
#endif  

  // did not collect memory; adjust constraints
  if(live_memory.size() >= allocated_memory.size() - 1) {
    if(uncollected_count < UNCOLLECTED_COUNT) {
      uncollected_count++;
    } 
    else {
      mem_max_size <<= 3;
      uncollected_count = 0;
    }
  }
  // collected memory; adjust constraints
  else if(mem_max_size != MEM_MAX) {
    if(collected_count < COLLECTED_COUNT) {
      collected_count++;
    } 
    else {
      mem_max_size = (mem_max_size >> 1) / 2;
      if(mem_max_size <= 0) {
        mem_max_size = MEM_MAX << 3;
      }
      collected_count = 0;
    }
  }

  // copy live memory to allocated memory
  allocated_memory = live_memory;
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _MEM_LOGGING
  mem_cycle++;
#endif

#ifdef _DEBUG_GC
  wcout << L"===============================================================" << endl;
  wcout << L"Finished Collection: collected=" << (start - allocation_size)
        << L" of " << start << L" byte(s) - " << showpoint << setprecision(3)
        << (((double)(start - allocation_size) / (double)start) * 100.0)
        << L"%" << endl;
  wcout << L"===============================================================" << endl;
#endif
  
#ifdef _TIMING
  end = clock();
  wcout << dec << L"Sweep time: " << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << endl;
#endif
  
#ifndef _WIN32
#ifndef _GC_SERIAL
  pthread_exit(nullptr);
#endif
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
  wcout << L"----- Marking Stack: stack: pos=" << info->stack_pos 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread() << L" -----" << endl;
#else
        << L"; thread=" << pthread_self() << L" -----" << endl;
#endif    
#endif


  while(info->stack_pos > -1) {
    size_t* check_mem = (size_t*)info->op_stack[info->stack_pos--];
#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    const bool found = allocated_memory.find(check_mem) != allocated_memory.end();
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
#endif
    if(found) {
      CheckObject(check_mem, false, 1);
    }
  }

  
  delete info;
  info = nullptr;

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
  wcout << L"---- Marking JIT method root(s): num=" << jit_frames.size()
#ifdef _WIN32
        << L"; thread=" << GetCurrentThread() << L" ------" << endl;
#else
        << L"; thread=" << pthread_self() << L" ------" << endl;
#endif    
  wcout << L"memory types: " << endl;
#endif
  
  for(size_t i = 0; i < jit_frames.size(); ++i) {
    StackFrame* frame = jit_frames[i];
    StackMethod* method = frame->method;
    size_t* mem = frame->jit_mem;
    size_t* self = (size_t*)frame->mem[0];
    const long dclrs_num = method->GetNumberDeclarations();

#ifdef _DEBUG_GC
    wcout << L"\t===== JIT method: name=" << method->GetName() << L", id=" << method->GetClass()->GetId()
      << L"," << method->GetId() << L"; addr=" << method << L"; mem=" << mem << L"; self=" << self
      << L"; num=" << method->GetNumberDeclarations() << L" =====" << endl;
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
          wcout << L"\t" << j << L": FUNC_PARM: id=(" << virtual_cls_id << L"," << mthd_id << L"), mem=" << lambda_mem << endl;
#endif
          pair<int, StackDclr**> closure_dclrs = prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(mthd_id);
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
          wcout << L"\t" << j << L": CHAR_PARM/INT_PARM: value=" << (*mem) << endl;
#endif
          // update
          mem++;
          break;

        case FLOAT_PARM: {
#ifdef _DEBUG_GC
          FLOAT_VALUE value;
          memcpy(&value, mem, sizeof(FLOAT_VALUE));
          wcout << L"\t" << j << L": FLOAT_PARM: value=" << value << endl;
#endif
          // update
          // TODO: mapped such that all 64-bit values the same size
#if defined(_WIN64) || defined(_X64) || defined(_ARM64)
          mem++;
#else
          mem += 2;
#endif
        }
          break;

        case BYTE_ARY_PARM:
#ifdef _DEBUG_GC
          wcout << L"\t" << j << L": BYTE_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case CHAR_ARY_PARM:
#ifdef _DEBUG_GC
          wcout << L"\t" << j << L": CHAR_ARY_PARM: addr=" << (size_t*)(*mem) << L"(" << (size_t)(*mem)
            << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case INT_ARY_PARM:
#ifdef _DEBUG_GC
          wcout << L"\t" << j << L": INT_ARY_PARM: addr=" << (size_t*)(*mem)
            << L"(" << (size_t)(*mem) << L"), size="
            << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case FLOAT_ARY_PARM:
#ifdef _DEBUG_GC
          wcout << L"\t" << j << L": FLOAT_ARY_PARM: addr=" << (size_t*)(*mem)
            << L"(" << (size_t)(*mem) << L"), size=" << L" byte(s)"
            << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) << endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case OBJ_PARM: {
#ifdef _DEBUG_GC
          wcout << L"\t" << j << L": OBJ_PARM: addr=" << (size_t*)(*mem)
            << L"(" << (size_t)(*mem) << L"), id=";
          if(*mem) {
            StackClass* tmp = (StackClass*)((size_t*)(*mem))[SIZE_OR_CLS];
            wcout << L"'" << tmp->GetName() << L"'" << endl;
          }
          else {
            wcout << L"Unknown" << endl;
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
          wcout << L"\t" << j << L": OBJ_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << endl;
#endif
          // mark data
          if(MarkValidMemory((size_t*)(*mem))) {
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
      // TODO: for non-ARM64 targets, skip 'has_and_or' variable addressed
#ifdef _ARM32
      // for ARM32, skip the link register
      for(int i = 1; i <= 6; ++i) {
#elif _ARM64
      mem = start;
      for(int i = 0; i > -6; --i) {
#else
      for(int i = 0; i < 6; ++i) {
#endif
        size_t* check_mem = (size_t*)mem[i];
#ifndef _GC_SERIAL
        MUTEX_LOCK(&allocated_lock);
#endif 
        const bool found = allocated_memory.find(check_mem) != allocated_memory.end();
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
      wcout << L"\t\t--- Nil memory ---" << endl;
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
  vector<StackFrame*> frames;

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_frame_lock);
#endif

#ifdef _DEBUG_GC
  wcout << L"----- PDA frames(s): num=" << pda_frames.size() 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread()<< L" -----" << endl;
#else
        << L"; thread=" << pthread_self() << L" -----" << endl;
#endif    
  wcout << L"memory types:" <<  endl;
#endif

  for(unordered_set<StackFrame**>::iterator iter = pda_frames.begin(); iter != pda_frames.end(); ++iter) {
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
  wcout << L"----- PDA method root(s): num=" << pda_monitors.size() 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread()<< L" -----" << endl;
#else
        << L"; thread=" << pthread_self()<< L" -----" << endl;
#endif    
  wcout << L"memory types:" <<  endl;
#endif

    // look at pda methods
  unordered_set<StackFrameMonitor*>::iterator pda_iter;
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
      frames.push_back(cur_frame);
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
    wcerr << L"Unable to create garbage collection thread!" << endl;
    exit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
  
  pthread_t jit_thread;
  if(pthread_create(&jit_thread, &attrs, CheckJitRoots, nullptr)) {
    cerr << L"Unable to create garbage collection thread!" << endl;
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
    wcout << L"\t===== PDA method: name=" << method->GetName() << L", addr="
      << method << L", num=" << method->GetNumberDeclarations() << L" =====" << endl;
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
    wcerr << L"Unable to join garbage collection threads!" << endl;
    exit(-1);
  }
  CloseHandle(thread_id);
#else
  void *status;
  if(pthread_join(jit_thread, &status)) {
    cerr << L"Unable to join garbage collection threads!" << endl;
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
      wcout << L"\t";
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
      wcout << L"\t" << i << L": FUNC_PARM: id=(" << virtual_cls_id << L"," << mthd_id << L"), mem=" << lambda_mem << endl;
#endif
      pair<int, StackDclr**> closure_dclrs = prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(mthd_id);
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
      wcout << L"\t" << i << L": CHAR_PARM/INT_PARM: value=" << (*mem) << endl;
#endif
      // update
      mem++;
      break;

    case FLOAT_PARM: {
#ifdef _DEBUG_GC
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
      wcout << L"\t" << i << L": FLOAT_PARM: value=" << value << endl;
#endif
      // update
      mem += 2;
    }
      break;

    case BYTE_ARY_PARM:
#ifdef _DEBUG_GC
      wcout << L"\t" << i << L": BYTE_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case CHAR_ARY_PARM:
#ifdef _DEBUG_GC
      wcout << L"\t" << i << L": CHAR_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) 
            << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case INT_ARY_PARM:
#ifdef _DEBUG_GC
      wcout << L"\t" << i << L": INT_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) 
            << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case FLOAT_ARY_PARM:
#ifdef _DEBUG_GC
      wcout << L"\t" << i << L": FLOAT_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) 
            << L" byte(s)" << endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case OBJ_PARM: {
#ifdef _DEBUG_GC
      wcout << L"\t" << i << L": OBJ_PARM: addr=" << (size_t*)(*mem) << L"(" << (size_t)(*mem) << L"), id=";
      if(*mem) {
        StackClass* tmp = (StackClass*)((size_t*)(*mem))[SIZE_OR_CLS];
        wcout << L"'" << tmp->GetName() << L"'" << endl;
      }
      else {
        wcout << L"Unknown" << endl;
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
      wcout << L"\t" << i << L": OBJ_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << endl;
#endif
      // mark data
      if(MarkValidMemory((size_t*)(*mem))) {
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

void MemoryManager::CheckObject(size_t* mem, bool is_obj, long depth)
{
  if(mem) {
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
        wcout << L"\t";
      }
      wcout << L"\t----- object: addr=" << mem << L"(" << (size_t)mem << L"), name='"
            << cls->GetName() << L"', num=" << cls->GetNumberInstanceDeclarations() << L" -----" << endl;
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
        wcout << L"\t";
      }
      wcout <<"$: addr/value=" << mem << endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(MarkValidMemory(mem)) {
        // ensure we're only checking int and obj arrays
        if(std::binary_search(allocated_memory.begin(), allocated_memory.end(), mem) && 
          (mem[TYPE] == NIL_TYPE || mem[TYPE] == INT_TYPE)) {
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

StackMethod* MemoryManager::GetVirtualEntry(StackClass* concrete_cls, size_t virtual_cls_id, size_t virtual_mthd_id)
{
  const size_t cantor_key = CantorHash((size_t)concrete_cls, virtual_cls_id, virtual_mthd_id);
	unordered_map<size_t, StackMethod*>::iterator result = virtual_method_table.find(cantor_key);
	if(result != virtual_method_table.end()) {
		return result->second;
	}

	return nullptr;
}

void MemoryManager::AddVirtualEntry(StackClass* concrete_cls, size_t virtual_cls_id, size_t virtual_mthd_id, StackMethod* mthd)
{
#ifndef _GC_SERIAL
	MUTEX_LOCK(&virtual_method_lock);
#endif
  const size_t cantor_key = CantorHash((size_t)concrete_cls, virtual_cls_id, virtual_mthd_id);
  virtual_method_table.insert(pair<size_t, StackMethod*>(cantor_key, mthd));
#ifndef _GC_SERIAL
	MUTEX_UNLOCK(&virtual_method_lock);
#endif
}
