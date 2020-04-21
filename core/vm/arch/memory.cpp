/***************************************************************************
* VM memory manager. Implements a "mark and sweep" collection algorithm.
*
* Copyright (c) 2008-2019, Randy Hollines
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

vector<size_t*> MemoryManager::allocated_memory;

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
#else
pthread_mutex_t MemoryManager::pda_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::pda_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::jit_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::allocated_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::marked_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::marked_sweep_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

void MemoryManager::Initialize(StackProgram* p)
{
  prgm = p;
  allocation_size = 0;
  mem_max_size = MEM_MAX;
  uncollected_count = 0;

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
#endif

  initialized = true;
}

// if return true, trace memory otherwise do not
inline bool MemoryManager::MarkMemory(size_t* mem)
{
  if(mem && ((size_t)mem) > MEM_START) {
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
  if(mem && ((size_t)mem) > MEM_START) {
#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    if(std::binary_search(allocated_memory.begin(), allocated_memory.end(), mem)) {
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&allocated_lock);
#endif
      
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
    else {
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&allocated_lock);
#endif
      return false;
    }
  }

  return false;
}

void MemoryManager::AddPdaMethodRoot(StackFrame** frame)
{
  if(!initialized) {
    return;
  }

#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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

#ifdef _DEBUG
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
#ifdef _DEBUG
  assert(cls);
#endif

  size_t* mem = nullptr;
  if(cls) {
    long size = cls->GetInstanceMemorySize();
#if defined(_WIN64) || defined(_X64)
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
#ifdef _DEBUG
    bool is_cached = false;
#endif
    const size_t alloc_size = size * 2 + sizeof(size_t) * EXTRA_BUF_SIZE;
    
    mem = (size_t*)calloc(alloc_size, sizeof(char));
    mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
    mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
    mem += EXTRA_BUF_SIZE;

    // record
 #ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
 #endif
    allocation_size += size;
    allocated_memory.push_back(mem);
 #ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
 #endif

#ifdef _MEM_LOGGING
    mem_logger << mem_cycle << L",alloc,obj," << mem << L"," << size << endl;
#endif

#ifdef _DEBUG
    wcout << L"# allocating object: cached=" << (is_cached ? L"true" : L"false")  << L", addr=" << mem << L"(" 
          << (size_t)mem << L"), size=" << size << L" byte(s), used=" << allocation_size << L" byte(s) #"
          << endl;
#endif
  }

  return mem;
}

size_t* MemoryManager::AllocateArray(const long size, const MemoryType type,  size_t* op_stack, long stack_pos, bool collect)
{
  if(size < 0) {
    wcerr << L">>> Invalid allocation size: " << size << L" <<<" << endl;
    exit(1);
  }

  size_t calc_size;
  size_t* mem;
  switch(type) {
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
  if(collect && allocation_size + calc_size > mem_max_size) {
    CollectAllMemory(op_stack, stack_pos);
  }

  // allocate memory
#ifdef _DEBUG
  bool is_cached = false;
#endif
  const size_t alloc_size = calc_size + sizeof(size_t) * EXTRA_BUF_SIZE;
  
  mem = (size_t*)calloc(alloc_size, sizeof(char));
  mem[EXTRA_BUF_SIZE + TYPE] = type;
  mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = calc_size;
  mem += EXTRA_BUF_SIZE;

#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
#endif
  allocation_size += calc_size;
  allocated_memory.push_back(mem);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _MEM_LOGGING
  mem_logger << mem_cycle << L",alloc,array," << mem << L"," << size << endl;
#endif

#ifdef _DEBUG
  wcout << L"# allocating array: cached=" << (is_cached ? L"true" : L"false") << L", addr=" << mem 
        << L"(" << (size_t)mem << L"), size=" << calc_size << L" byte(s), used=" << allocation_size 
        << L" byte(s) #" << endl;
#endif

  return mem;
}

size_t* MemoryManager::ValidObjectCast(size_t* mem, long to_id, int* cls_hierarchy, int** cls_interfaces)
{
  // invalid array cast  
  long id = GetObjectID(mem);
  if (id < 0) {
    return nullptr;
  }

  // upcast
  int cls_id = id;
  while (cls_id != -1) {
    if (cls_id == to_id) {
      return mem;
    }
    // update
    cls_id = cls_hierarchy[cls_id];
  }

  // check interfaces
  cls_id = id;
  while (cls_id != -1) {
    int* interfaces = cls_interfaces[cls_id];
    if (interfaces) {
      int i = 0;
      int inf_id = interfaces[i];
      while (inf_id > -1) {
        if (inf_id == to_id) {
          return mem;
        }
        inf_id = interfaces[++i];
      }
    }
    // update
    cls_id = cls_hierarchy[cls_id];
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

#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
#endif
  std::sort(allocated_memory.begin(), allocated_memory.end());
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _DEBUG
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
#ifdef _DEBUG
  wcout << L"## Sweeping memory ##" << endl;
#endif

  // sort and search
#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
  MUTEX_LOCK(&marked_lock);
#endif

#ifdef _DEBUG
  wcout << L"-----------------------------------------" << endl;
  wcout << L"Sweeping..." << endl;
  wcout << L"-----------------------------------------" << endl;
#endif

#ifndef _GC_SERIAL

#endif
  vector<size_t*> live_memory;
  for(size_t i = 0; i < allocated_memory.size(); ++i) {
    size_t* mem = allocated_memory[i];

    // check dynamic memory
    bool found = false;
    if(mem[MARKED_FLAG]) {
      mem[MARKED_FLAG] = 0L;
      found = true;
    }

    // live
    if(found) {
      live_memory.push_back(mem);
    }
    // will be collected
    else {
      // object or array  
      size_t mem_size;
      if(mem[TYPE] == NIL_TYPE) {
        StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
#ifdef _DEBUG
        assert(cls);
#endif
        if(cls) {
#if defined(_WIN64) || defined(_X64)
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
      free(tmp);
      tmp = nullptr;
#ifdef _DEBUG
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

#ifdef _DEBUG
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
#ifdef _DEBUG
  wcout << L"----- Marking Stack: stack: pos=" << info->stack_pos 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread() << L" -----" << endl;
#else
        << L"; thread=" << pthread_self() << L" -----" << endl;
#endif    
#endif
  while(info->stack_pos > -1) {
    CheckObject((size_t*)info->op_stack[info->stack_pos--], false, 1);
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

#ifdef _DEBUG
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

#ifdef _DEBUG
    wcout << L"\t===== JIT method: name=" << method->GetName() << L", id=" << method->GetClass()->GetId()
      << L"," << method->GetId() << L"; addr=" << method << L"; mem=" << mem << L"; self=" << self
      << L"; num=" << method->GetNumberDeclarations() << L" =====" << endl;
#endif

    if(mem && ((size_t)mem) > MEM_START) {
      // check self
      if(!method->IsLambda()) {
        CheckObject(self, true, 1);
      }

      StackDclr** dclrs = method->GetDeclarations();
      for(int j = dclrs_num - 1; j > -1; j--) {
        // update address based upon type
        switch(dclrs[j]->type) {
        case FUNC_PARM: {
          size_t* lambda_mem = (size_t*) * (mem + 1);
          const size_t mthd_cls_id = *mem;
          const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
          const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
#ifdef _DEBUG
          wcout << L"\t" << j << L": FUNC_PARM: id=(" << cls_id << L"," << mthd_id << L"), mem=" << lambda_mem << endl;
#endif
          pair<int, StackDclr**> closure_dclrs = prgm->GetClass(cls_id)->GetClosureDeclarations(mthd_id);
          if(MarkMemory(lambda_mem)) {
            CheckMemory(lambda_mem, closure_dclrs.second, closure_dclrs.first, 1);
          }
          // update
          mem += 2;
        }
          break;

        case CHAR_PARM:
        case INT_PARM:
#ifdef _DEBUG
          wcout << L"\t" << j << L": CHAR_PARM/INT_PARM: value=" << (*mem) << endl;
#endif
          // update
          mem++;
          break;

        case FLOAT_PARM: {
#ifdef _DEBUG
          FLOAT_VALUE value;
          memcpy(&value, mem, sizeof(FLOAT_VALUE));
          wcout << L"\t" << j << L": FLOAT_PARM: value=" << value << endl;
#endif
          // update
          // TODO: mapped such that all 64-bit values the same size
#if defined(_WIN64) || defined(_X64)
          mem++;
#else
          mem += 2;
#endif
        }
          break;

        case BYTE_ARY_PARM:
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
      // during some method calls. there are 6 integer temp addresses
#ifdef _ARM32
      // for ARM32, skip the link register
      for(int i = 1; i <= 6; ++i) {
#else
      for(int i = 0; i < 6; ++i) {
#endif
        CheckObject((size_t*)mem[i], false, 1);
      }
    }
#ifdef _DEBUG
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

#ifdef _DEBUG
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

#ifdef _DEBUG
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

#ifdef _DEBUG
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
#ifdef _DEBUG
    for(int j = 0; j < depth; ++j) {
      wcout << L"\t";
    }
#endif

    // update address based upon type
    switch(dclrs[i]->type) {
    case FUNC_PARM: {
      size_t* lambda_mem = (size_t*) * (mem + 1);
      const size_t mthd_cls_id = *mem;
      const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
      const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
#ifdef _DEBUG
      wcout << L"\t" << i << L": FUNC_PARM: id=(" << cls_id << L"," << mthd_id << L"), mem=" << lambda_mem << endl;
#endif
      pair<int, StackDclr**> closure_dclrs = prgm->GetClass(cls_id)->GetClosureDeclarations(mthd_id);
      if(MarkMemory(lambda_mem)) {
        CheckMemory(lambda_mem, closure_dclrs.second, closure_dclrs.first, depth + 1);
      }
      // update
      mem += 2;
    }
      break;

  case CHAR_PARM:
    case INT_PARM:
#ifdef _DEBUG
      wcout << L"\t" << i << L": CHAR_PARM/INT_PARM: value=" << (*mem) << endl;
#endif
      // update
      mem++;
      break;

    case FLOAT_PARM: {
#ifdef _DEBUG
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
      wcout << L"\t" << i << L": FLOAT_PARM: value=" << value << endl;
#endif
      // update
      mem += 2;
    }
      break;

    case BYTE_ARY_PARM:
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
  if(mem && ((size_t)mem) > MEM_START) {
    StackClass* cls;
    if(is_obj) {
      cls = GetClass(mem);
    }
    else {
      cls = GetClassMapping(mem);
    }

    if(cls) {
#ifdef _DEBUG
      for(int i = 0; i < depth; ++i) {
        wcout << L"\t";
      }
      wcout << L"\t----- object: addr=" << mem << L"(" << (size_t)mem << L"), num="
            << cls->GetNumberInstanceDeclarations() << L" -----" << endl;
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
#ifdef _DEBUG
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
