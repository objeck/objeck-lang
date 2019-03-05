#include <iostream>
#include <list>

using namespace std;

#define SIZE_DELTA 64

class FreeMemoryManager {
  const size_t pool_max_size;
  size_t alloc_pool_size;
  list<size_t*> pool;

  size_t* FindMemory(size_t size) {
    size_t* raw_mem = NULL;
    bool found = false;
    std::list<size_t*>::iterator iter = pool.begin();
    for(; !found && iter != pool.end(); ++iter) {
      raw_mem = *iter;
      const size_t alloc_size = raw_mem[0];
      if(alloc_size >= size && alloc_size < size + SIZE_DELTA) {
        wcout << "Cached: size=" << size << L" alloc_size=" << alloc_size << endl;
        found = true;
      }
    }

    // remove
    if(found) {
      pool.remove(*iter);
      return raw_mem;
    }

    return NULL;
  }

  void FreePool() {
    wcout << L"Freeing" << endl;

    while(!pool.empty()) {
      size_t* raw_mem = pool.front();
      pool.pop_front();

      const size_t alloc_size = raw_mem[0];

      wcout << L"\tOld: alloc_size=" << alloc_size << endl;

      free(raw_mem);
      raw_mem = NULL;
      alloc_pool_size -= alloc_size;
    }
  }

public:
  FreeMemoryManager(const size_t m) : pool_max_size(m) {
    alloc_pool_size = 0;
  }

  ~FreeMemoryManager() {
  }

  size_t* GetMemory(size_t size) {
    size_t* raw_mem = FindMemory(size);

    if(!raw_mem) {
      const size_t alloc_size = size + sizeof(size_t);
      alloc_pool_size += alloc_size;
      if(alloc_pool_size > pool_max_size) {
        FreePool();
        if(alloc_pool_size > pool_max_size) {
          return NULL;
        }
      }
      raw_mem = (size_t*)calloc(alloc_size, sizeof(char));
      raw_mem[0] = alloc_size;

      wcout << "New: size=" << size << L" alloc_size=" << alloc_size << endl; 
    }

    wcout << L"\tGet: raw=" << raw_mem << L", size=" << size << L", alloc=" << alloc_pool_size << L", max=" << pool_max_size << endl;
    
    return raw_mem + 1;
  }

  void FreeMemory(size_t* mem) {
    size_t* raw_mem = mem - 1;    
    const size_t alloc_size = raw_mem[0];
    alloc_pool_size -= alloc_size;
    pool.push_back(raw_mem);

    wcout << L"Free: alloc=" << alloc_size << L", max=" << pool_max_size << L", pool=" << pool.size() << endl;
  }
};

int main() {
  FreeMemoryManager mm(1024);

  size_t* m0 = mm.GetMemory(512);
  size_t* m1 = mm.GetMemory(256);
  size_t* m2 = mm.GetMemory(128);
  size_t* m3 = mm.GetMemory(64);

  mm.FreeMemory(m1);
  mm.FreeMemory(m3);
  m1 = mm.GetMemory(200);
  m3 = mm.GetMemory(512);



  return 0;
}
