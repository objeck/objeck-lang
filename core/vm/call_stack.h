/***************************************************************************
 * A VM thread's call stack and operand stack.
 *
 * Copyright (c) 2026, Randy Hollines
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

#include "common.h"
#include <cstddef>
#include <cstdint>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

// What bounds a call, and why there are three limits.
//
// The call stack used to be a fixed array of CALL_STACK_SIZE (256) frame
// pointers per thread, with a fixed operand stack of 768 words beside it. 256
// frames is not deep: the Rosetta "Man or boy" test at k=10 needs 647, and a
// recursive-descent parser fails on input nested 300 deep. The new limit is a
// maximum DEPTH, set once per process (--call-depth, OBJECK_CALL_DEPTH; default
// CALL_DEPTH_DEFAULT), and three things bound a call against it:
//
//  1. The frame array. Each thread RESERVES address space for the maximum and
//     commits it as the stack deepens (Grow), so a thread that never recurses
//     holds one page of it, not the maximum. depth_limit is the committed part;
//     reaching it takes the slow path, which commits more until the maximum.
//
//  2. The operand stack. An interpreted frame can leave values on it across a
//     call (`Sum(n - 1) + n` keeps n there): measured, one word per frame for
//     plain recursion. It was never checked. With 256 frames and 768 words that
//     rarely mattered; with thousands of frames it would write past the end. It
//     is reserved and committed the same way, OP_SLOTS_PER_FRAME words per frame
//     plus OP_HEADROOM, and an interpreted call checks that OP_HEADROOM words are
//     left (op_limit) -- the most any one method's expressions had before.
//
//  3. The native stack. A compiled method's frame is on the machine stack
//     (measured about 520 bytes for a small recursive method on x64), and so is
//     every interpreter a JIT bridge starts. Thousands of compiled frames
//     overflowed a 1 MB Windows thread stack -- a crash, not a diagnostic.
//     native_floor is the lowest address a call may start below; it leaves
//     NativeMargin() bytes for whatever native code runs at the deepest frame.
//     The compiled call site, CallCompiled and the interpreter's call into
//     compiled code all test it, so running out of native stack is reported
//     exactly as a full call stack is, and recovers the same way.
//
// The JIT backends read pos, depth_limit and native_floor at fixed offsets from
// the call_stack_pos pointer they already hold, so this struct's head is ABI.

#define CALL_DEPTH_DEFAULT 16384
#define CALL_DEPTH_MIN 256
#define CALL_DEPTH_MAX 1048576

// byte offsets from call_stack_pos (the struct's address), for the JIT
#define CALL_STACK_LIMIT_OFFSET 8
#define CALL_STACK_FLOOR_OFFSET 16

namespace Runtime {
  // The process's maximum call depth in frames; fixed before the first thread
  // starts (ApplyVmOptions).
  inline long& MaxCallDepthRef() {
    static long depth = CALL_DEPTH_DEFAULT;
    return depth;
  }

  inline long MaxCallDepth() {
    return MaxCallDepthRef();
  }

  // Native stack for a thread the VM starts. Windows: enough for the maximum
  // depth in compiled frames of up to 1 KB plus 2 MB, at least 16 MB; it is
  // reserved, and only what a thread touches is backed (measured: the working
  // set of 1,000 idle threads did not change). POSIX: 8 MB, Linux's default and
  // the size of a POSIX main thread's stack, unless --call-depth asks for more
  // than the default; macOS's own default of 512 KB held only a few hundred
  // compiled frames. Larger stacks are not free there: where transparent huge
  // pages are on for every mapping (THP "always"), glibc lets 2 MB pages back a
  // thread stack, and the resident size grew with the stack -- measured on
  // Linux, 100 idle threads held 48 MB with 8 MB stacks and 86 MB with 18 MB
  // ones. A 32-bit build keeps the platform default.
  inline size_t NativeThreadStackBytes() {
#if UINTPTR_MAX > 0xFFFFFFFFu
    const size_t mb = 1048576ULL;
    size_t bytes = (size_t)MaxCallDepth() * 1024ULL + 2 * mb;
#ifdef _WIN32
    const size_t least = 16 * mb;
#else
    const size_t least = 8 * mb;
    if(MaxCallDepth() <= CALL_DEPTH_DEFAULT) {
      bytes = least;
    }
#endif
    if(bytes < least) {
      bytes = least;
    }
    return (bytes + mb - 1) & ~(mb - 1);
#else
    return 0;
#endif
  }

  struct CallStack {
    // --- read by compiled code; offsets are fixed (see above) ---
    long pos;                    // 0: the frame count; call_stack_pos points here
    int64_t depth_limit;         // 8: frames committed; a call at pos >= this grows or fails
    uintptr_t native_floor;      // 16: a call below this native address fails; 0 = unknown

    // --- C++ only ---
    // the highest operand-stack index at which an interpreted call may start;
    // SIZE_MAX while the thread's operand stack is not this one (a native
    // library's callback brings its own)
    size_t op_limit;
    StackFrame** frames;
    size_t* op_stack;
    size_t frames_reserved;      // bytes
    size_t frames_committed;
    size_t op_reserved;
    size_t op_committed;

    static const size_t OP_SLOTS_PER_FRAME = 8;
    static const size_t OP_HEADROOM = 1024;
    static const size_t FRAMES_INITIAL = 4096;      // bytes: 512 frames
    static const size_t OP_INITIAL = 32768;         // bytes: 4096 words

    static CallStack* Create() {
      CallStack* stack = new CallStack;
      stack->pos = -1;
      stack->depth_limit = 0;
      stack->native_floor = CurrentNativeFloor();
      stack->op_limit = SIZE_MAX;
      stack->frames = nullptr;
      stack->op_stack = nullptr;
      stack->frames_reserved = stack->frames_committed = 0;
      stack->op_reserved = stack->op_committed = 0;

      const size_t depth = (size_t)MaxCallDepth();
      stack->frames_reserved = RoundToPage(depth * sizeof(StackFrame*));
      stack->frames = (StackFrame**)Reserve(stack->frames_reserved);
      if(!stack->frames || !stack->CommitFrames(FRAMES_INITIAL)) {
        throw std::bad_alloc();
      }
      return stack;
    }

    static void Destroy(CallStack* stack) {
      if(!stack) {
        return;
      }
      Release(stack->frames, stack->frames_reserved);
      Release(stack->op_stack, stack->op_reserved);
      delete stack;
    }

    // The operand stack for the thread that owns this call stack, reserved on
    // first use. Interpreted calls check it from then on.
    size_t* OpStack() {
      if(!op_stack) {
        op_reserved = RoundToPage(((size_t)MaxCallDepth() * OP_SLOTS_PER_FRAME + OP_HEADROOM) * sizeof(size_t));
        op_stack = (size_t*)Reserve(op_reserved);
        if(!op_stack || !CommitOp(OP_INITIAL)) {
          throw std::bad_alloc();
        }
      }
      return op_stack;
    }

    // Room for one more call: a frame slot at pos and OP_HEADROOM operand words
    // above op_pos. Commits more when the committed part is used up; false at
    // the maximum (or when the OS refuses the memory).
    // Out of line: the callers' fast paths only compare.
#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    bool Grow(size_t op_pos) {
      if(pos >= depth_limit) {
        const size_t want = frames_committed * 2;
        if(frames_committed >= frames_reserved || !CommitFrames(want < frames_reserved ? want : frames_reserved)) {
          return false;
        }
        if(pos >= depth_limit) {
          return false;
        }
      }
      while(op_pos > op_limit) {
        const size_t want = op_committed * 2;
        if(op_committed >= op_reserved || !CommitOp(want < op_reserved ? want : op_reserved)) {
          return false;
        }
      }
      return true;
    }

    // true when the native stack is too low for another call
    inline bool NativeStackLow() const {
      volatile char probe = 0;
      return (uintptr_t)&probe < native_floor;
    }

    // what this thread's frames and operand stack hold committed, in bytes
    size_t CommittedBytes() const {
      return frames_committed + op_committed;
    }

    static CallStack* FromPos(long* call_stack_pos) {
      return reinterpret_cast<CallStack*>(call_stack_pos);
    }

    // Bytes of native stack kept free below the deepest call: for the native
    // code a compiled frame or a trap runs there (a library call, the
    // collector's work on this thread, printing the diagnostic).
    static size_t NativeMargin(size_t stack_bytes) {
      const size_t mb = 1048576;
      if(stack_bytes >= 16 * mb) {
        return mb;
      }
      if(stack_bytes >= 4 * mb) {
        return mb / 2;
      }
      return stack_bytes / 8;
    }

    // The current OS thread's native floor, computed once per thread.
    static uintptr_t CurrentNativeFloor() {
      static thread_local uintptr_t floor = 0;
      static thread_local bool known = false;
      if(!known) {
        known = true;
        uintptr_t low = 0;
        uintptr_t high = 0;
        if(NativeStackBounds(low, high) && high > low) {
          floor = low + NativeMargin(high - low);
        }
      }
      return floor;
    }

  private:
    bool CommitFrames(size_t bytes) {
      bytes = RoundToPage(bytes);
      if(bytes > frames_reserved) {
        bytes = frames_reserved;
      }
      if(bytes <= frames_committed) {
        return true;
      }
      if(!Commit(frames, frames_committed, bytes)) {
        return false;
      }
      frames_committed = bytes;
      size_t limit = bytes / sizeof(StackFrame*);
      if(limit > (size_t)MaxCallDepth()) {
        limit = (size_t)MaxCallDepth();
      }
      // a reader of depth_limit is this thread or its compiled code: no fence
      depth_limit = (int64_t)limit;
      return true;
    }

    bool CommitOp(size_t bytes) {
      bytes = RoundToPage(bytes);
      if(bytes > op_reserved) {
        bytes = op_reserved;
      }
      if(bytes <= op_committed) {
        return true;
      }
      if(!Commit(op_stack, op_committed, bytes)) {
        return false;
      }
      op_committed = bytes;
      op_limit = bytes / sizeof(size_t) - OP_HEADROOM;
      return true;
    }

    static size_t PageSize() {
#ifdef _WIN32
      return 4096;
#else
      static const size_t page = (size_t)sysconf(_SC_PAGESIZE);
      return page;
#endif
    }

    static size_t RoundToPage(size_t bytes) {
      const size_t page = PageSize();
      return (bytes + page - 1) / page * page;
    }

    // address space only; no memory is used until Commit
    static void* Reserve(size_t bytes) {
#ifdef _WIN32
      return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS);
#else
      void* mem = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
      return mem == MAP_FAILED ? nullptr : mem;
#endif
    }

    // makes [from, to) of a reservation usable; the pages are zero-filled and
    // backed by memory when first touched
    static bool Commit(void* base, size_t from, size_t to) {
#ifdef _WIN32
      return VirtualAlloc((char*)base + from, to - from, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
      return mprotect((char*)base + from, to - from, PROT_READ | PROT_WRITE) == 0;
#endif
    }

    static void Release(void* base, size_t bytes) {
      if(!base) {
        return;
      }
#ifdef _WIN32
      (void)bytes;
      VirtualFree(base, 0, MEM_RELEASE);
#else
      munmap(base, bytes);
#endif
    }

    // [low, high) of the calling thread's native stack
    static bool NativeStackBounds(uintptr_t& low, uintptr_t& high) {
      volatile char probe = 0;
#ifdef _WIN32
      // The stack is one reservation; its AllocationBase is the lowest address.
      // The thread is near the top of it here, at its start, so the address of
      // a local stands in for the top.
      MEMORY_BASIC_INFORMATION info;
      if(!VirtualQuery((const void*)&probe, &info, sizeof(info))) {
        return false;
      }
      low = (uintptr_t)info.AllocationBase;
      high = (uintptr_t)&probe;
      return true;
#elif defined(_OSX)
      pthread_t self = pthread_self();
      high = (uintptr_t)pthread_get_stackaddr_np(self);
      const size_t size = pthread_get_stacksize_np(self);
      if(!high || !size) {
        return false;
      }
      low = high - size;
      (void)probe;
      return true;
#elif defined(__GLIBC__)
      pthread_attr_t attr;
      if(pthread_getattr_np(pthread_self(), &attr)) {
        return false;
      }
      void* addr = nullptr;
      size_t size = 0;
      const int status = pthread_attr_getstack(&attr, &addr, &size);
      pthread_attr_destroy(&attr);
      if(status || !addr || !size) {
        return false;
      }
      low = (uintptr_t)addr;
      high = low + size;
      // the main thread's reported size is RLIMIT_STACK, which can be
      // unlimited; never trust a bound above this frame
      if((uintptr_t)&probe < low || (uintptr_t)&probe >= high) {
        return false;
      }
      return true;
#else
      (void)probe;
      (void)low;
      (void)high;
      return false;
#endif
    }
  };

  static_assert(offsetof(CallStack, pos) == 0, "compiled code reads pos at offset 0");
  static_assert(offsetof(CallStack, depth_limit) == CALL_STACK_LIMIT_OFFSET, "compiled code reads depth_limit at CALL_STACK_LIMIT_OFFSET");
  static_assert(offsetof(CallStack, native_floor) == CALL_STACK_FLOOR_OFFSET, "compiled code reads native_floor at CALL_STACK_FLOOR_OFFSET");
}
