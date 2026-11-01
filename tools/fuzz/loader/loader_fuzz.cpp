/***************************************************************************
 * libFuzzer target for the VM's bytecode loader (Loader::Load).
 *
 * Two targets are built from this file (tools/fuzz/loader/build.sh):
 *
 *   loader_fuzz       the input is a decompressed program image, the bytes
 *                     Loader::Load reads after the file layer has inflated it.
 *                     This is where the mutations do their work.
 *   loader_file_fuzz  (LOADER_FUZZ_FILE) the input is an .obe file as written
 *                     by obc: [uncompressed size:4][zlib] or raw zlib. It
 *                     covers the file layer (Loader::DecodeImage) and then
 *                     loads whatever decodes.
 *
 * A rejected input is a clean result: the loader prints its message (silenced
 * here) and, in this build only, throws LoaderFuzzReject instead of calling
 * exit(1). A method signature the VM cannot parse throws std::runtime_error
 * from StackMethod's constructor, which obr reports as a load error; that is
 * clean too. Anything else -- a sanitizer report, a signal, a timeout, an
 * out-of-memory -- is a finding.
 ***************************************************************************/

#include "../../../core/vm/loader.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
  // The loader reports every rejection on stderr; at tens of thousands of
  // inputs a second that is most of the run. Fuzzer and sanitizer reports go
  // through stdio, not these streams, so they are unaffected.
  std::wcerr.setstate(std::ios::badbit);
  std::wcout.setstate(std::ios::badbit);
  return 0;
}

static void LoadImage(const char* image, size_t size)
{
  // two program arguments, so the argument strings are exercised as well
  std::vector<std::wstring> args = { L"first", L"second" };
  Loader* loader = new Loader(image, size, args);
  try {
    loader->Load();
  }
  catch(const LoaderFuzzReject&) {
  }
  catch(const std::runtime_error&) {
  }
  delete loader;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
#ifdef LOADER_FUZZ_FILE
  size_t image_size = 0;
  char* image = Loader::DecodeImage(reinterpret_cast<const char*>(data), size, image_size);
  if(image) {
    LoadImage(image, image_size);
    free(image);
  }
#else
  LoadImage(reinterpret_cast<const char*>(data), size);
#endif
  return 0;
}
