// Standalone check of NATIVE_GUARD / APITools_Guard (lib_api.h). Not part of any
// build; compile and run by hand from core/vm:
//
//   g++ -std=c++17 -I. lib_api_guard_test.cpp -o guard_test && ./guard_test
//
// Exits non-zero if an exception escapes the guard, if the last-error message is not
// recorded, or if the non-error path is disturbed.
#include "lib_api.h"

#include <new>
#include <stdexcept>

static int failures = 0;

static void check(bool ok, const char* what) {
  if(!ok) {
    std::cerr << "FAIL: " << what << std::endl;
    failures++;
  }
}

// shaped like a library entry point: the exception must not leave it
extern "C" void guarded_entry(VMContext& context, int mode, int* result) {
  const bool ok = NATIVE_GUARD(context, {
    if(mode == 1) {
      throw std::runtime_error("boom");
    }
    if(mode == 2) {
      throw std::bad_alloc();
    }
    if(mode == 3) {
      throw 42;
    }
    // a comma outside parentheses must survive the macro
    std::pair<int, int> pair(1, 2);
    *result = pair.first + pair.second;
  });
  if(!ok) {
    *result = -1;
  }
}

int main() {
  VMContext context;
  int result = 0;

  guarded_entry(context, 0, &result);
  check(result == 3, "non-error path runs the body");
  check(APITools_GetLastError().empty(), "non-error path leaves no error");

  guarded_entry(context, 1, &result);
  check(result == -1, "std::exception is caught");
  check(APITools_GetLastError() == L"boom", "std::exception message is recorded");

  guarded_entry(context, 2, &result);
  check(result == -1, "std::bad_alloc is caught");
  check(!APITools_GetLastError().empty(), "bad_alloc message is recorded");

  guarded_entry(context, 3, &result);
  check(result == -1, "a non-std exception is caught");
  check(APITools_GetLastError() == L"unknown native exception", "unknown exception is recorded");

  guarded_entry(context, 0, &result);
  check(result == 3 && APITools_GetLastError().empty(), "a later success clears the error");

  if(failures) {
    return 1;
  }
  std::cout << "PASS: native guard" << std::endl;
  return 0;
}
