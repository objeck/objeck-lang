/* Probe: does this ONNX Runtime build expose the training API?
   Loads the runtime by path (so any build can be tested without relinking),
   asks for the inference API at the header's version, then calls
   GetTrainingApi at that version and at 1. */
#include <stdio.h>
#include <stdint.h>
#include "onnxruntime_c_api.h"

#ifdef _WIN32
#include <windows.h>
#define LOAD(p) (void*)LoadLibraryA(p)
#define SYM(h, n) (void*)GetProcAddress((HMODULE)(h), n)
#else
#include <dlfcn.h>
#define LOAD(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#define SYM(h, n) dlsym(h, n)
#endif

typedef const OrtApiBase* (ORT_API_CALL *GetApiBaseFn)(void);

int main(int argc, char** argv) {
  if(argc < 2) {
    fprintf(stderr, "usage: %s <path to onnxruntime library>\n", argv[0]);
    return 2;
  }

  void* lib = LOAD(argv[1]);
  if(!lib) {
    fprintf(stderr, "cannot load %s\n", argv[1]);
    return 2;
  }

  GetApiBaseFn get_base = (GetApiBaseFn)SYM(lib, "OrtGetApiBase");
  if(!get_base) {
    fprintf(stderr, "no OrtGetApiBase in %s\n", argv[1]);
    return 2;
  }

  const OrtApiBase* base = get_base();
  printf("library:          %s\n", argv[1]);
  printf("runtime version:  %s\n", base->GetVersionString());
  printf("header version:   %d\n", ORT_API_VERSION);

  const OrtApi* api = base->GetApi(ORT_API_VERSION);
  if(!api) {
    printf("GetApi(%d):        NULL (runtime older than header)\n", ORT_API_VERSION);
    return 2;
  }

  const OrtTrainingApi* t = api->GetTrainingApi(ORT_API_VERSION);
  printf("GetTrainingApi(%d): %s\n", ORT_API_VERSION, t ? "non-NULL" : "NULL");
  const OrtTrainingApi* t1 = api->GetTrainingApi(1);
  printf("GetTrainingApi(1):  %s\n", t1 ? "non-NULL" : "NULL");

  return (t || t1) ? 0 : 1;
}
