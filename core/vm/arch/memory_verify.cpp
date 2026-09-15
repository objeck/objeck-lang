/***************************************************************************
* Heap verifier for the generational collector (OBJECK_GC_VERIFY).
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

//
// OBJECK_GC_VERIFY=<N>|checkmark turns the verifier on; it is read once, in
// MemoryManager::Initialize. With it unset, MemoryManager::gc_verify is 0 and
// every hook in the collector is one predictable branch.
//
// Where it runs: only on the collecting thread, inside stop-the-world
// (marked_sweep_lock held, every other mutator parked), never from a mark
// thread, and never while holding allocated_lock -- it takes pda_frame_lock and
// pda_monitor_lock one at a time, as FixupRoots does, and allocated_lock above
// either would be the ABBA order documented in CollectMemory. The one exception
// is invariant C, which runs inside fixup under allocated_lock and so takes no
// lock at all. old_generation is read without allocated_lock: every thread that
// could insert into it is parked.
//
// What it trusts: only references the collector can TYPE -- class statics,
// declared frame slots (interpreter and JIT), declared instance fields and
// closure captures. Never operand stacks, JIT temp windows or pending-thread
// roots, which legitimately hold stale words. Int[] and object arrays share
// INT_TYPE, so an array element is only ever a warning.
//
// Invariants (docs/PLAN_2026_10_0_HARDENING.md, 1.5):
//   A1  checkmark: young objects reachable by a typed trace before a minor GC
//       are all marked by its mark phase (OBJECK_GC_VERIFY=checkmark only)
//   A2  an old object with a typed nursery reference has GC_RSET_BIT and is in
//       dirty_list unless the list overflowed
//   B1  old-generation headers are sane after a collection
//   B2  typed references are 0 or live old-generation objects of the right TYPE
//   B3  no typed reference points into the nursery after a collection
//   B4  typed frame slots obey B2/B3
//   C   fixup never meets a typed reference to a young object it cannot forward
//
// Cost tiers: T0 (every collection) checks the dirty list, the objects promoted
// and dirtied by this collection, statics and frames; T1 (the first 8
// collections, then every Nth) walks the whole old generation; T2 is A1.
//
// OBJECK_GC_VERIFY_INJECT=field|barrier|mark (honoured only with the verifier
// on) plants a fault so the verifier can be shown to catch one.
//

#include "memory.h"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>

#define INJECT_NONE MemoryManager::GC_VERIFY_INJECT_NONE
#define INJECT_FIELD MemoryManager::GC_VERIFY_INJECT_FIELD
#define INJECT_BARRIER MemoryManager::GC_VERIFY_INJECT_BARRIER
#define INJECT_MARK MemoryManager::GC_VERIFY_INJECT_MARK

namespace {
  // stands in for a ParamType when an array element's type is unknown
  const int UNTYPED_PARM = 0;

  const long FIRST_FULL_CHECKS = 8;
  const long MAX_VIOLATIONS = 16;
  const long MAX_WARNINGS = 8;
  const int MAX_CLOSURE_DEPTH = 64;

  std::string ReadEnv(const char* name) {
#ifdef _WIN32
    size_t len = 0;
    if(getenv_s(&len, nullptr, 0, name) || !len) {
      return "";
    }
    std::string value(len, '\0');
    if(getenv_s(&len, &value[0], len, name)) {
      return "";
    }
    value.resize(len > 0 ? len - 1 : 0);
    return value;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
  }

  // Where a reference was found, for the report
  struct Origin {
    size_t* holder;                // object, closure block, class memory or frame memory
    const std::wstring* name;      // class or method name; nullptr = describe the heap holder
    const wchar_t* kind;           // L"object", L"static", L"frame", L"closure", L"array"
    long index;                    // declaration, slot or element index
    bool typed;
  };
}

class GcVerifier {
public:
  static long period;
  static bool checkmark;
  static std::atomic<long> collection;
  static bool minor;
  static const wchar_t* phase;
  static long violations;
  static long warnings;
  static std::unordered_set<StackClass*> classes;
  static std::vector<size_t*> dirty_snapshot;
  static std::vector<size_t*> promoted;
  static bool dirty_overflow;
  static std::unordered_set<size_t*> dirty_set;
  // A1: young objects the typed trace reached, and whether any path was typed
  static std::unordered_map<size_t*, Origin> expected_young;
  static bool expected_built;
  // timing, for the T1 back-off
  static std::chrono::steady_clock::time_point window_start;
  static double window_verify_s;
  // injection state
  static bool field_injected;
  static bool mark_injected;
  static std::atomic<long> barrier_skip_epoch;
  static std::atomic<size_t*> barrier_skip_target;
  static std::atomic<bool> barrier_noted;

  //
  // Reporting
  //
  static std::wstring Hex(size_t value) {
    std::wostringstream out;
    out << L"0x" << std::hex << value;
    return out.str();
  }

  static std::wstring Describe(size_t* mem) {
    if(!mem) {
      return L"?";
    }
    switch(mem[TYPE]) {
    case instructions::NIL_TYPE: {
      StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
      return classes.count(cls) ? cls->GetName() : L"<unknown class " + Hex((size_t)cls) + L">";
    }
    case instructions::BYTE_ARY_TYPE:
      return L"Byte[] or closure block";
    case instructions::CHAR_ARY_TYPE:
      return L"Char[]";
    case instructions::INT_TYPE:
      return L"Int[] or object array";
    case instructions::FLOAT_TYPE:
      return L"Float[]";
    default:
      return L"<invalid TYPE " + std::to_wstring(mem[TYPE]) + L">";
    }
  }

  static std::wstring HolderName(const Origin& origin) {
    if(origin.name) {
      return *origin.name;
    }
    return Describe(origin.holder);
  }

  [[noreturn]] static void Fail() {
    std::wcerr << L">>> gc-verify: " << violations << L" violation(s) in collection " << collection.load()
               << L"; aborting <<<" << std::endl;
    std::wcerr.flush();
    fflush(stderr);
    // A program's own SIGABRT handler (Runtime->SetSignal) must not swallow this
    std::signal(SIGABRT, SIG_DFL);
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    abort();
  }

  static void Report(const wchar_t* invariant, const Origin& origin, size_t value, const std::wstring& detail) {
    std::wcerr << L">>> gc-verify: " << invariant << L" violation in collection " << collection.load()
               << L" (" << (minor ? L"minor" : L"major") << L", " << phase << L"): " << origin.kind
               << L"=" << Hex((size_t)origin.holder) << L" class='" << HolderName(origin) << L"' field="
               << origin.index << L" value=" << Hex(value) << L": " << detail << L" <<<" << std::endl;
    if(++violations >= MAX_VIOLATIONS) {
      Fail();
    }
  }

  static void Warn(const wchar_t* invariant, const Origin& origin, size_t value, const std::wstring& detail) {
    if(warnings++ < MAX_WARNINGS) {
      std::wcerr << L"[gc-verify] warning (" << invariant << L", collection " << collection.load() << L"): "
                 << origin.kind << L"=" << Hex((size_t)origin.holder) << L" class='" << HolderName(origin)
                 << L"' element=" << origin.index << L" value=" << Hex(value) << L": " << detail << std::endl;
      if(warnings == MAX_WARNINGS) {
        std::wcerr << L"[gc-verify] further warnings suppressed" << std::endl;
      }
    }
  }

  //
  // Heap predicates
  //
  static uint8_t* YoungStart() { return MemoryManager::young_region; }

  static bool InNurseryRange(size_t value) {
    const uint8_t* p = (uint8_t*)value;
    return p >= MemoryManager::young_region && p < MemoryManager::young_region + MemoryManager::young_region_size;
  }

  static bool IsYoung(size_t value) {
    return MemoryManager::IsYoung((size_t*)value);
  }

  static bool IsOld(size_t value) {
    return value && MemoryManager::old_generation.count((size_t*)value);
  }

  // A live nursery address that is the start of a real object. The header words
  // are read only once the address is far enough into the region for them to
  // exist (IsYoungCandidate alone guarantees one word, the header needs four).
  static bool IsYoungObject(size_t value) {
    size_t* mem = (size_t*)value;
    if(!MemoryManager::IsYoungCandidate(mem) ||
       (uint8_t*)mem < MemoryManager::young_region + sizeof(size_t) * (1 + EXTRA_BUF_SIZE)) {
      return false;
    }
    if(mem[TYPE] != instructions::NIL_TYPE) {
      return false;
    }
    StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
    return classes.count(cls) && MemoryManager::IsYoungObjectStart(mem, cls);
  }

  static bool IsValidType(size_t type) {
    return type >= instructions::NIL_TYPE && type <= instructions::FLOAT_TYPE;
  }

  // the memory TYPE a declaration requires of a non-zero reference; 0 = any
  static size_t ExpectedType(int parm) {
    switch(parm) {
    case instructions::BYTE_ARY_PARM:
    case instructions::FUNC_PARM:
      return instructions::BYTE_ARY_TYPE;
    case instructions::CHAR_ARY_PARM:
      return instructions::CHAR_ARY_TYPE;
    case instructions::INT_ARY_PARM:
    case instructions::OBJ_ARY_PARM:
      return instructions::INT_TYPE;
    case instructions::FLOAT_ARY_PARM:
      return instructions::FLOAT_TYPE;
    default:
      return 0;
    }
  }

  static bool IsReferenceParm(int parm) {
    switch(parm) {
    case instructions::OBJ_PARM:
    case instructions::OBJ_ARY_PARM:
    case instructions::BYTE_ARY_PARM:
    case instructions::CHAR_ARY_PARM:
    case instructions::INT_ARY_PARM:
    case instructions::FLOAT_ARY_PARM:
      return true;
    default:
      return false;
    }
  }

  static bool ClosureDeclarations(size_t id_word, std::pair<int, StackDclr**>& dclrs) {
    const long cls_id = (long)((id_word >> 16) & 0xFFFF);
    const int mthd_id = (int)(id_word & 0xFFFF);
    StackClass* cls = MemoryManager::prgm->GetClass(cls_id);
    if(!cls) {
      return false;
    }
    dclrs = cls->GetClosureDeclarations(mthd_id);
    return dclrs.second || !dclrs.first;
  }

  static void EnsureClasses() {
    if(!classes.empty()) {
      return;
    }
    StackClass** clss = MemoryManager::prgm->GetClasses();
    const long cls_num = MemoryManager::prgm->GetClassNumber();
    for(long i = 0; i < cls_num; ++i) {
      if(clss[i]) {
        classes.insert(clss[i]);
      }
    }
  }

  static bool IsFullCheck() {
    const long n = collection.load();
    return n <= FIRST_FULL_CHECKS || n % period == 0;
  }

  //
  // Walkers
  //
  template<class F> static void ForEachDeclaration(size_t* mem, StackDclr** dclrs, long count, F&& f) {
    if(!mem) {
      return;
    }
    for(long i = 0; i < count; ++i) {
      const int type = dclrs[i]->type;
      f(i, type, mem);
      mem += type == instructions::FUNC_PARM ? 2 : 1;
    }
  }

  // Mirrors the collector's frame walks (CheckPdaRoots, CheckJitRoots, FixupRoots)
  template<class F> static void ForEachFrameSlot(StackFrame* frame, F&& f) {
    StackMethod* method = frame->method;
    StackDclr** dclrs = method->GetDeclarations();
    const long count = method->GetNumberDeclarations();
    if(frame->jit_mem) {
      size_t* mem = frame->jit_mem;
#ifdef _ARM64
      if(method->HasAndOr()) {
        mem++;
      }
      for(long j = 0; j < count; ++j) {
#else
      for(long j = count - 1; j >= 0; --j) {
#endif
        const int type = dclrs[j]->type;
        f(j, type, mem);
        mem += type == instructions::FUNC_PARM ? 2 : 1;
      }
    }
    else if(frame->mem) {
      ForEachDeclaration(frame->mem + (method->HasAndOr() ? 2 : 1), dclrs, count, f);
    }
  }

  static void GatherFrames(std::vector<StackFrame*>& frames) {
#ifndef _GC_SERIAL
    MUTEX_LOCK(&MemoryManager::pda_frame_lock);
#endif
    for(auto iter = MemoryManager::pda_frames.begin(); iter != MemoryManager::pda_frames.end(); ++iter) {
      if(**iter) {
        frames.push_back(**iter);
      }
    }
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&MemoryManager::pda_frame_lock);
    MUTEX_LOCK(&MemoryManager::pda_monitor_lock);
#endif
    for(auto iter = MemoryManager::pda_monitors.begin(); iter != MemoryManager::pda_monitors.end(); ++iter) {
      StackFrameMonitor* monitor = *iter;
      long call_stack_pos = *(monitor->call_stack_pos);
      std::atomic_thread_fence(std::memory_order_acquire);
      if(call_stack_pos >= 0) {
        StackFrame* cur_frame = *(monitor->cur_frame);
        if(cur_frame) {
          frames.push_back(cur_frame);
        }
        for(long i = call_stack_pos - 1; i >= 0; --i) {
          if(monitor->call_stack[i]) {
            frames.push_back(monitor->call_stack[i]);
          }
        }
      }
    }
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&MemoryManager::pda_monitor_lock);
#endif
  }

  //
  // A2: before a collection, an old object holding a typed nursery reference
  // must be in the remembered set
  //
  static void RequireRemembered(size_t* holder, const Origin& origin, size_t value) {
    if(!IsYoung(value)) {
      return;
    }
    const size_t flags = holder[MARKED_FLAG];
    if(!(flags & GC_RSET_BIT)) {
      const std::wstring detail = L"old object holds a nursery reference but GC_RSET_BIT is clear (missing write barrier)";
      if(origin.typed) {
        Report(L"A2", origin, value, detail);
      }
      else {
        Warn(L"A2", origin, value, detail);
      }
    }
    else if(!dirty_overflow && !dirty_set.count(holder)) {
      const std::wstring detail = L"old object has GC_RSET_BIT but is not in dirty_list";
      if(origin.typed) {
        Report(L"A2", origin, value, detail);
      }
      else {
        Warn(L"A2", origin, value, detail);
      }
    }
  }

  static void CheckRememberedDeclarations(size_t* holder, const Origin& parent, StackDclr** dclrs, long count, int depth) {
    ForEachDeclaration(holder, dclrs, count, [&](long i, int type, size_t* slot) {
      Origin origin = parent;
      origin.index = i;
      if(IsReferenceParm(type)) {
        RequireRemembered(holder, origin, *slot);
      }
      else if(type == instructions::FUNC_PARM) {
        const size_t closure = slot[1];
        RequireRemembered(holder, origin, closure);
        std::pair<int, StackDclr**> closure_dclrs;
        if(depth < MAX_CLOSURE_DEPTH && IsOld(closure) && ((size_t*)closure)[TYPE] == instructions::BYTE_ARY_TYPE &&
           ClosureDeclarations(slot[0], closure_dclrs)) {
          const Origin inner = { (size_t*)closure, nullptr, L"closure", 0, true };
          CheckRememberedDeclarations((size_t*)closure, inner, closure_dclrs.second, closure_dclrs.first, depth + 1);
        }
      }
    });
  }

  static void CheckRemembered(size_t* obj) {
    switch(obj[TYPE]) {
    case instructions::NIL_TYPE: {
      StackClass* cls = (StackClass*)obj[SIZE_OR_CLS];
      if(classes.count(cls)) {
        const Origin origin = { obj, nullptr, L"object", 0, true };
        CheckRememberedDeclarations(obj, origin, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), 0);
      }
    }
      break;

    case instructions::INT_TYPE: {
      size_t count, dim;
      if(ArrayExtents(obj, count, dim)) {
        size_t* elements = obj + 2 + dim;
        for(size_t k = 0; k < count; ++k) {
          const Origin origin = { obj, nullptr, L"array", (long)k, false };
          RequireRemembered(obj, origin, elements[k]);
        }
      }
    }
      break;

    default:
      break;
    }
  }

  // An Int/Float array's element count and dimension count, if they fit its block
  static bool ArrayExtents(size_t* obj, size_t& count, size_t& dim) {
    const size_t words = obj[SIZE_OR_CLS] / sizeof(size_t);
    if(words < 2) {
      return false;
    }
    count = obj[0];
    dim = obj[1];
    return dim <= words && count <= words && count + dim + 2 <= words;
  }

  //
  // A1: typed trace from the collector's typed roots, before a minor GC
  //
  struct Work {
    size_t* mem;
    StackDclr** dclrs;   // closure captures only; an object's class supplies them
    long count;
    bool typed;          // array elements are object references
    bool closure;
  };

  static std::vector<Work> work;
  static std::unordered_map<size_t*, bool> visited;

  static void TraceClosure(size_t id_word, size_t closure, const Origin& origin) {
    if(!closure) {
      return;
    }
    if(!IsOld(closure)) {
      Report(L"B2", origin, closure, L"closure reference is not a live old-generation block");
      return;
    }
    auto found = visited.find((size_t*)closure);
    if(found != visited.end()) {
      return;
    }
    visited.emplace((size_t*)closure, true);
    std::pair<int, StackDclr**> closure_dclrs;
    if(ClosureDeclarations(id_word, closure_dclrs)) {
      work.push_back({ (size_t*)closure, closure_dclrs.second, closure_dclrs.first, true, true });
    }
  }

  static void TraceReference(size_t value, int parm, const Origin& origin) {
    if(!value) {
      return;
    }
    if(IsYoung(value)) {
      if(!IsYoungObject(value)) {
        if(origin.typed) {
          Report(L"B2", origin, value, L"typed reference to a nursery address that is not an object start");
        }
        return;
      }
      auto found = expected_young.find((size_t*)value);
      if(found == expected_young.end()) {
        expected_young.emplace((size_t*)value, origin);
        work.push_back({ (size_t*)value, nullptr, 0, false, false });
      }
      else if(origin.typed && !found->second.typed) {
        found->second = origin;
      }
      return;
    }
    if(IsOld(value)) {
      size_t* mem = (size_t*)value;
      const size_t type = mem[TYPE];
      if(type != instructions::NIL_TYPE && type != instructions::INT_TYPE) {
        return;
      }
      const bool typed_elements = type == instructions::INT_TYPE && parm == instructions::OBJ_ARY_PARM && origin.typed;
      auto found = visited.find(mem);
      if(found == visited.end()) {
        visited.emplace(mem, typed_elements);
        work.push_back({ mem, nullptr, 0, typed_elements, false });
      }
      else if(typed_elements && !found->second) {
        found->second = true;
        work.push_back({ mem, nullptr, 0, true, false });
      }
      return;
    }
    if(origin.typed && parm != UNTYPED_PARM) {
      Report(L"B2", origin, value, L"typed reference is not a heap object (dangling or corrupt)");
    }
  }

  static void TraceDeclarations(size_t* mem, const Origin& parent, StackDclr** dclrs, long count) {
    ForEachDeclaration(mem, dclrs, count, [&](long i, int type, size_t* slot) {
      Origin origin = parent;
      origin.index = i;
      if(IsReferenceParm(type)) {
        TraceReference(*slot, type, origin);
      }
      else if(type == instructions::FUNC_PARM) {
        TraceClosure(slot[0], slot[1], origin);
      }
    });
  }

  static void BuildExpectedYoung() {
    expected_young.clear();
    visited.clear();
    work.clear();

    // class statics
    StackClass** clss = MemoryManager::prgm->GetClasses();
    const long cls_num = MemoryManager::prgm->GetClassNumber();
    for(long i = 0; i < cls_num; ++i) {
      StackClass* cls = clss[i];
      const Origin origin = { cls->GetClassMemory(), &cls->GetName(), L"static", 0, true };
      TraceDeclarations(cls->GetClassMemory(), origin, cls->GetClassDeclarations(), cls->GetNumberClassDeclarations());
    }

    // typed frame slots and self
    std::vector<StackFrame*> frames;
    GatherFrames(frames);
    for(size_t i = 0; i < frames.size(); ++i) {
      StackFrame* frame = frames[i];
      StackMethod* method = frame->method;
      const Origin self_origin = { frame->mem, &method->GetName(), L"frame", -1, false };
      if(!method->IsLambda() && frame->mem) {
        TraceReference(frame->mem[0], UNTYPED_PARM, self_origin);
      }
      ForEachFrameSlot(frame, [&](long j, int type, size_t* slot) {
        const Origin origin = { frame->jit_mem ? frame->jit_mem : frame->mem, &method->GetName(), L"frame", j, true };
        if(IsReferenceParm(type)) {
          TraceReference(*slot, type, origin);
        }
        else if(type == instructions::FUNC_PARM) {
          TraceClosure(slot[0], slot[1], origin);
        }
      });
    }

    // transitive closure through old and young objects alike
    while(!work.empty()) {
      const Work item = work.back();
      work.pop_back();
      if(item.closure) {
        const Origin origin = { item.mem, nullptr, L"closure", 0, true };
        TraceDeclarations(item.mem, origin, item.dclrs, item.count);
      }
      else if(item.mem[TYPE] == instructions::NIL_TYPE) {
        StackClass* cls = (StackClass*)item.mem[SIZE_OR_CLS];
        if(classes.count(cls)) {
          const Origin origin = { item.mem, nullptr, L"object", 0, true };
          TraceDeclarations(item.mem, origin, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations());
        }
      }
      else {
        size_t count, dim;
        if(ArrayExtents(item.mem, count, dim)) {
          size_t* elements = item.mem + 2 + dim;
          for(size_t k = 0; k < count; ++k) {
            const Origin origin = { item.mem, nullptr, L"array", (long)k, item.typed };
            TraceReference(elements[k], item.typed ? (int)instructions::OBJ_PARM : UNTYPED_PARM, origin);
          }
        }
      }
    }
    visited.clear();
    expected_built = true;
  }

  //
  // B1-B4: after a collection
  //
  static bool CheckReferenceAfter(const Origin& origin, int parm, size_t value) {
    if(!value) {
      return true;
    }
    if(InNurseryRange(value)) {
      Report(L"B3", origin, value, L"typed reference points into the nursery after the collection");
      return false;
    }
    if(!IsOld(value)) {
      Report(L"B2", origin, value, L"typed reference is not a live old-generation object");
      return false;
    }
    const size_t type = ((size_t*)value)[TYPE];
    const size_t expected = ExpectedType(parm);
    if(expected && type != expected) {
      Report(L"B2", origin, value, L"typed reference has the wrong memory TYPE " + std::to_wstring(type) +
             L" (expected " + std::to_wstring(expected) + L")");
      return false;
    }
    return true;
  }

  static void CheckDeclarationsAfter(size_t* mem, const Origin& parent, StackDclr** dclrs, long count, int depth) {
    ForEachDeclaration(mem, dclrs, count, [&](long i, int type, size_t* slot) {
      Origin origin = parent;
      origin.index = i;
      CheckSlotAfter(origin, type, slot, depth);
    });
  }

  static void CheckSlotAfter(const Origin& origin, int type, size_t* slot, int depth) {
    if(IsReferenceParm(type)) {
      CheckReferenceAfter(origin, type, *slot);
    }
    else if(type == instructions::FUNC_PARM) {
      const size_t closure = slot[1];
      std::pair<int, StackDclr**> closure_dclrs;
      if(closure && CheckReferenceAfter(origin, type, closure) && depth < MAX_CLOSURE_DEPTH &&
         ClosureDeclarations(slot[0], closure_dclrs)) {
        const Origin inner = { (size_t*)closure, nullptr, L"closure", 0, true };
        CheckDeclarationsAfter((size_t*)closure, inner, closure_dclrs.second, closure_dclrs.first, depth + 1);
      }
    }
  }

  static void CheckHeader(size_t* obj) {
    const Origin origin = { obj, nullptr, L"object", -1, true };
    const size_t flags = obj[MARKED_FLAG];
    if(!(flags & GC_OLD_BIT)) {
      Report(L"B1", origin, flags, L"old-generation object lacks GC_OLD_BIT");
    }
    if(flags & (GC_MARK_BIT | GC_RSET_BIT)) {
      Report(L"B1", origin, flags, L"mark or remembered-set bit left set after the collection");
    }
    const size_t block = obj[-(long)(1 + EXTRA_BUF_SIZE)];
    const size_t header = sizeof(size_t) * EXTRA_BUF_SIZE;
    const size_t type = obj[TYPE];
    if(type == instructions::NIL_TYPE) {
      StackClass* cls = (StackClass*)obj[SIZE_OR_CLS];
      if(!classes.count(cls)) {
        Report(L"B1", origin, (size_t)cls, L"class pointer is not a loaded class");
        return;
      }
      const long inst_size = cls->GetInstanceMemorySize();
      // objects are allocated at their exact instance size (AllocateObject, G2)
      if(inst_size < 0 || block < (size_t)inst_size + header) {
        Report(L"B1", origin, block, L"block is smaller than the class instance");
      }
      return;
    }
    if(!IsValidType(type)) {
      Report(L"B1", origin, type, L"invalid memory TYPE");
      return;
    }
    const size_t bytes = obj[SIZE_OR_CLS];
    if(block < bytes + header) {
      Report(L"B1", origin, block, L"block is smaller than the array's byte size " + std::to_wstring(bytes));
      return;
    }
    size_t count, dim;
    switch(type) {
    case instructions::INT_TYPE:
    case instructions::FLOAT_TYPE:
      if(!ArrayExtents(obj, count, dim)) {
        Report(L"B1", origin, obj[0], L"array extents run past the block");
      }
      break;

    case instructions::CHAR_ARY_TYPE:
      if(bytes >= 2 * sizeof(size_t)) {
        count = obj[0];
        dim = obj[1];
        if(dim > bytes / sizeof(size_t) || count > bytes / sizeof(wchar_t) ||
           (dim + 2) * sizeof(size_t) + count * sizeof(wchar_t) > bytes) {
          Report(L"B1", origin, obj[0], L"character array extents run past the block");
        }
      }
      break;

    default:
      // Byte[] shares BYTE_ARY_TYPE with closure capture blocks, which have no extents
      break;
    }
  }

  static void CheckObjectAfter(size_t* obj, bool full) {
    CheckHeader(obj);
    switch(obj[TYPE]) {
    case instructions::NIL_TYPE: {
      StackClass* cls = (StackClass*)obj[SIZE_OR_CLS];
      if(classes.count(cls)) {
        const Origin origin = { obj, nullptr, L"object", 0, true };
        CheckDeclarationsAfter(obj, origin, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), 0);
      }
    }
      break;

    case instructions::INT_TYPE:
      if(full) {
        size_t count, dim;
        if(ArrayExtents(obj, count, dim)) {
          size_t* elements = obj + 2 + dim;
          for(size_t k = 0; k < count; ++k) {
            if(InNurseryRange(elements[k])) {
              const Origin origin = { obj, nullptr, L"array", (long)k, false };
              Warn(L"B3", origin, elements[k], L"array element points into the nursery after the collection");
            }
          }
        }
      }
      break;

    default:
      break;
    }
  }

  // B3 for a closure capture block dirtied before the collection. Such a block has no
  // declarations until a holder types it, so the typed walks above never look inside
  // one that no holder reaches yet. A word still naming a nursery object start (the
  // nursery is not cleared, so the stale header is still there) is a capture the
  // collection promoted without forwarding the block's copy.
  static void CheckCaptureBlockAfter(size_t* obj) {
    const size_t words = obj[SIZE_OR_CLS] / sizeof(size_t);
    for(size_t k = 0; k < words; ++k) {
      const size_t value = obj[k];
      size_t* mem = (size_t*)value;
      if(!InNurseryRange(value) || (value & (sizeof(size_t) - 1)) ||
         (uint8_t*)mem < MemoryManager::young_region + sizeof(size_t) * (1 + EXTRA_BUF_SIZE)) {
        continue;
      }
      if(mem[TYPE] == instructions::NIL_TYPE && classes.count((StackClass*)mem[SIZE_OR_CLS]) &&
         MemoryManager::IsYoungObjectStart(mem, (StackClass*)mem[SIZE_OR_CLS])) {
        const Origin origin = { obj, nullptr, L"capture block", (long)k, true };
        Report(L"B3", origin, value, L"dirty closure capture block still refers to a nursery object after the collection (capture not forwarded)");
      }
    }
  }

  static void CheckRootsAfter() {
    StackClass** clss = MemoryManager::prgm->GetClasses();
    const long cls_num = MemoryManager::prgm->GetClassNumber();
    for(long i = 0; i < cls_num; ++i) {
      StackClass* cls = clss[i];
      const Origin origin = { cls->GetClassMemory(), &cls->GetName(), L"static", 0, true };
      CheckDeclarationsAfter(cls->GetClassMemory(), origin, cls->GetClassDeclarations(), cls->GetNumberClassDeclarations(), 0);
    }

    std::vector<StackFrame*> frames;
    GatherFrames(frames);
    for(size_t i = 0; i < frames.size(); ++i) {
      StackFrame* frame = frames[i];
      StackMethod* method = frame->method;
      if(!method->IsLambda() && frame->mem && InNurseryRange(frame->mem[0])) {
        const Origin origin = { frame->mem, &method->GetName(), L"frame", -1, true };
        Report(L"B4", origin, frame->mem[0], L"self points into the nursery after the collection");
      }
      ForEachFrameSlot(frame, [&](long j, int type, size_t* slot) {
        const Origin origin = { frame->jit_mem ? frame->jit_mem : frame->mem, &method->GetName(),
                                frame->jit_mem ? L"jit-frame" : L"frame", j, true };
        CheckSlotAfter(origin, type, slot, 0);
      });
    }
  }

  //
  // Fault injection
  //
  // offset of the first OBJ_PARM instance field, or -1
  static long FirstObjectField(StackClass* cls) {
    StackDclr** dclrs = cls->GetInstanceDeclarations();
    const long count = cls->GetNumberInstanceDeclarations();
    long offset = 0;
    for(long i = 0; i < count; ++i) {
      if(dclrs[i]->type == instructions::OBJ_PARM) {
        return offset;
      }
      offset += dclrs[i]->type == instructions::FUNC_PARM ? 2 : 1;
    }
    return -1;
  }

  static void InjectField() {
    for(size_t i = 0; i < promoted.size(); ++i) {
      size_t* obj = promoted[i];
      if(obj[TYPE] == instructions::NIL_TYPE && classes.count((StackClass*)obj[SIZE_OR_CLS])) {
        StackClass* cls = (StackClass*)obj[SIZE_OR_CLS];
        const long offset = FirstObjectField(cls);
        if(offset >= 0) {
          obj[offset] |= 1;
          field_injected = true;
          std::wcerr << L"[gc-verify] inject=field: ORed 1 into word " << offset << L" of promoted '"
                     << cls->GetName() << L"' " << Hex((size_t)obj) << std::endl;
          return;
        }
      }
    }
  }

  // ORs the mark bit into a surviving young object's reference field: the word a
  // stale conservative root one word past that field would mark (#816's shape)
  static void InjectMark() {
    const size_t young_used = MemoryManager::young_offset.load(std::memory_order_relaxed);
    uint8_t* scan_ptr = MemoryManager::young_region;
    while(scan_ptr < MemoryManager::young_region + young_used) {
      size_t* raw_mem = (size_t*)scan_ptr;
      const size_t total = (sizeof(size_t) + raw_mem[0] + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
      size_t* mem = raw_mem + 1 + EXTRA_BUF_SIZE;
      if(mem[TYPE] == instructions::NIL_TYPE && (mem[MARKED_FLAG] & GC_MARK_BIT) &&
         classes.count((StackClass*)mem[SIZE_OR_CLS])) {
        StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
        const long offset = FirstObjectField(cls);
        if(offset >= 0) {
          mem[offset] |= GC_MARK_BIT;
          mark_injected = true;
          std::wcerr << L"[gc-verify] inject=mark: ORed the mark bit into word " << offset << L" of young '"
                     << cls->GetName() << L"' " << Hex((size_t)mem) << std::endl;
          return;
        }
      }
      if(!total) {
        return;
      }
      scan_ptr += total;
    }
  }

  //
  // Bookkeeping
  //
  static void Finish(const std::chrono::steady_clock::time_point& start) {
    if(violations) {
      Fail();
    }
    const auto now = std::chrono::steady_clock::now();
    window_verify_s += std::chrono::duration<double>(now - start).count();
  }

  // T1 back-off: if full walks cost more than ~20% of wall time, walk half as often
  static void AdjustPeriod() {
    if(checkmark) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const double wall_s = std::chrono::duration<double>(now - window_start).count();
    if(wall_s < 2.0) {
      return;
    }
    const double fraction = window_verify_s / wall_s;
    if(fraction > 0.2 && collection.load() > FIRST_FULL_CHECKS) {
      period *= 2;
      std::wcerr << L"[gc-verify] verification took " << (long)(fraction * 100.0)
                 << L"% of wall time; full heap walks now every " << period << L" collections" << std::endl;
    }
    window_start = now;
    window_verify_s = 0.0;
  }
};

long GcVerifier::period = 1;
bool GcVerifier::checkmark = false;
std::atomic<long> GcVerifier::collection(0);
bool GcVerifier::minor = false;
const wchar_t* GcVerifier::phase = L"before";
long GcVerifier::violations = 0;
long GcVerifier::warnings = 0;
std::unordered_set<StackClass*> GcVerifier::classes;
std::vector<size_t*> GcVerifier::dirty_snapshot;
std::vector<size_t*> GcVerifier::promoted;
bool GcVerifier::dirty_overflow = false;
std::unordered_set<size_t*> GcVerifier::dirty_set;
std::unordered_map<size_t*, Origin> GcVerifier::expected_young;
bool GcVerifier::expected_built = false;
std::chrono::steady_clock::time_point GcVerifier::window_start;
double GcVerifier::window_verify_s = 0.0;
bool GcVerifier::field_injected = false;
bool GcVerifier::mark_injected = false;
std::atomic<long> GcVerifier::barrier_skip_epoch(-1);
std::atomic<size_t*> GcVerifier::barrier_skip_target(nullptr);
std::atomic<bool> GcVerifier::barrier_noted(false);
std::vector<GcVerifier::Work> GcVerifier::work;
std::unordered_map<size_t*, bool> GcVerifier::visited;

int MemoryManager::gc_verify = 0;
int MemoryManager::gc_verify_inject = 0;

void MemoryManager::VerifyInitialize()
{
  gc_verify = 0;
  gc_verify_inject = INJECT_NONE;

  const std::string mode = ReadEnv("OBJECK_GC_VERIFY");
  if(mode.empty() || mode == "0") {
    return;
  }
  if(mode == "checkmark") {
    GcVerifier::checkmark = true;
    GcVerifier::period = 1;
  }
  else {
    char* end = nullptr;
    const long period = strtol(mode.c_str(), &end, 10);
    if(!end || *end || period < 1) {
      std::wcerr << L">>> OBJECK_GC_VERIFY: expected a positive collection period or 'checkmark' <<<" << std::endl;
      exit(1);
    }
    GcVerifier::period = period;
  }

  const std::string inject = ReadEnv("OBJECK_GC_VERIFY_INJECT");
  if(inject == "field") {
    gc_verify_inject = INJECT_FIELD;
  }
  else if(inject == "barrier") {
    gc_verify_inject = INJECT_BARRIER;
  }
  else if(inject == "mark") {
    gc_verify_inject = INJECT_MARK;
  }
  else if(!inject.empty()) {
    std::wcerr << L">>> OBJECK_GC_VERIFY_INJECT: expected 'field', 'barrier' or 'mark' <<<" << std::endl;
    exit(1);
  }

  GcVerifier::window_start = std::chrono::steady_clock::now();
  gc_verify = 1;
}

// After the stop-the-world handshake, before ScanDirtyObject (minor) or the mark
// phase (major)
void MemoryManager::VerifyBeforeCollection(bool minor)
{
  const auto start = std::chrono::steady_clock::now();
  GcVerifier::collection.fetch_add(1);
  GcVerifier::minor = minor;
  GcVerifier::phase = L"before";
  GcVerifier::EnsureClasses();
  GcVerifier::expected_built = false;

  const size_t dc = dirty_count.load(std::memory_order_acquire);
  GcVerifier::dirty_overflow = dc > DIRTY_LIST_MAX;
  const size_t listed = GcVerifier::dirty_overflow ? DIRTY_LIST_MAX : dc;
  GcVerifier::dirty_snapshot.assign(dirty_list, dirty_list + listed);
  GcVerifier::dirty_set.clear();
  GcVerifier::dirty_set.insert(GcVerifier::dirty_snapshot.begin(), GcVerifier::dirty_snapshot.end());

  // T0, A2: every dirty_list entry is a tracked old-generation object
  for(size_t i = 0; i < listed; ++i) {
    size_t* obj = dirty_list[i];
    const Origin origin = { obj, nullptr, L"dirty_list", (long)i, true };
    if(!GcVerifier::IsOld((size_t)obj)) {
      GcVerifier::Report(L"A2", { nullptr, nullptr, L"dirty_list", (long)i, true }, (size_t)obj,
                         L"dirty_list entry is not a live old-generation object");
    }
    else if((obj[MARKED_FLAG] & (GC_OLD_BIT | GC_RSET_BIT)) != (GC_OLD_BIT | GC_RSET_BIT)) {
      GcVerifier::Report(L"A2", origin, obj[MARKED_FLAG], L"dirty_list entry lacks GC_OLD_BIT or GC_RSET_BIT");
    }
  }

  // T0, N1: every nursery block's object address is one the collector's young range
  // tests accept. This is checked on the nursery's own layout, not through a trace,
  // because the traces classify addresses with the same IsYoung/IsYoungCandidate the
  // collector uses: an object whose address fell outside that half-open range (a
  // zero-field object ending exactly at young_offset, before ObjectBlockSize padded
  // it) looked like "not young" to both, was never marked, promoted or forwarded, and
  // the only sign of it was an untyped B3 warning on the array holding it, several
  // collections later. The walk is linear in nursery objects and runs every collection.
  {
    const size_t young_used = young_offset.load(std::memory_order_acquire);
    uint8_t* scan_ptr = young_region;
    while(young_region && scan_ptr < young_region + young_used) {
      size_t* raw_mem = (size_t*)scan_ptr;
      const size_t block = raw_mem[0];
      const size_t total = (sizeof(size_t) + block + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
      size_t* mem = raw_mem + 1 + EXTRA_BUF_SIZE;
      const Origin origin = { nullptr, nullptr, L"nursery", (long)((uint8_t*)raw_mem - young_region), true };
      if(block < sizeof(size_t) * EXTRA_BUF_SIZE || scan_ptr + total > young_region + young_used) {
        GcVerifier::Report(L"N1", origin, block, L"nursery block size word runs past the used nursery");
        break;
      }
      if(!IsYoungCandidate(mem)) {
        GcVerifier::Report(L"N1", origin, (size_t)mem,
                           L"nursery object's address is outside the collector's young range (block of " +
                           std::to_wstring(block) + L" bytes ends at the nursery offset): it cannot be marked or forwarded");
      }
      else if(mem[TYPE] == instructions::NIL_TYPE && !IsYoungObjectStart(mem, (StackClass*)mem[SIZE_OR_CLS])) {
        GcVerifier::Report(L"N1", origin, block, L"nursery object's size word does not match its class instance size");
      }
      scan_ptr += total;
    }
  }

  // A2 over the whole old generation (T1), else over what the last collection promoted (T0)
  if(GcVerifier::IsFullCheck()) {
    for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
      GcVerifier::CheckRemembered(*iter);
    }
  }
  else {
    for(size_t i = 0; i < GcVerifier::promoted.size(); ++i) {
      if(GcVerifier::IsOld((size_t)GcVerifier::promoted[i])) {
        GcVerifier::CheckRemembered(GcVerifier::promoted[i]);
      }
    }
  }

  // T2, A1
  if(GcVerifier::checkmark && minor) {
    GcVerifier::BuildExpectedYoung();
  }

  GcVerifier::Finish(start);
}

// In CollectMemory, after every mark thread has joined and before promotion
// rewrites the nursery's flag words
void MemoryManager::VerifyAfterMark()
{
  const auto start = std::chrono::steady_clock::now();
  GcVerifier::phase = L"after mark";

  if(gc_verify_inject == INJECT_MARK && !GcVerifier::mark_injected) {
    GcVerifier::InjectMark();
  }

  if(GcVerifier::expected_built) {
    for(auto iter = GcVerifier::expected_young.begin(); iter != GcVerifier::expected_young.end(); ++iter) {
      size_t* young = iter->first;
      if(!(young[MARKED_FLAG] & GC_MARK_BIT)) {
        const std::wstring detail = L"young '" + GcVerifier::Describe(young) +
          L"' reachable through this reference was not marked by the minor GC";
        if(iter->second.typed) {
          GcVerifier::Report(L"A1", iter->second, (size_t)young, detail);
        }
        else {
          GcVerifier::Warn(L"A1", iter->second, (size_t)young, detail);
        }
      }
    }
    GcVerifier::expected_young.clear();
    GcVerifier::expected_built = false;
  }

  GcVerifier::Finish(start);
}

void MemoryManager::VerifyNotePromoted(std::vector<size_t*>& promoted)
{
  GcVerifier::promoted.swap(promoted);
}

// After CollectMemory returns, still inside stop-the-world
void MemoryManager::VerifyAfterCollection(bool minor)
{
  const auto start = std::chrono::steady_clock::now();
  GcVerifier::minor = minor;
  GcVerifier::phase = L"after";

  if(gc_verify_inject == INJECT_FIELD && !GcVerifier::field_injected) {
    GcVerifier::InjectField();
  }

  const bool full = GcVerifier::IsFullCheck();

  // T0: what this collection promoted and what was dirty going in (a full walk
  // covers both)
  if(!full) {
    for(size_t i = 0; i < GcVerifier::promoted.size(); ++i) {
      if(GcVerifier::IsOld((size_t)GcVerifier::promoted[i])) {
        GcVerifier::CheckObjectAfter(GcVerifier::promoted[i], false);
      }
    }
    for(size_t i = 0; i < GcVerifier::dirty_snapshot.size(); ++i) {
      if(GcVerifier::IsOld((size_t)GcVerifier::dirty_snapshot[i])) {
        GcVerifier::CheckObjectAfter(GcVerifier::dirty_snapshot[i], false);
      }
    }
  }
  GcVerifier::CheckRootsAfter();

  // T0, B3: dirty closure capture blocks, which no typed walk may reach yet
  for(size_t i = 0; i < GcVerifier::dirty_snapshot.size(); ++i) {
    size_t* obj = GcVerifier::dirty_snapshot[i];
    if(GcVerifier::IsOld((size_t)obj) && obj[TYPE] == instructions::BYTE_ARY_TYPE) {
      GcVerifier::CheckCaptureBlockAfter(obj);
    }
  }

  // T1: every old-generation object
  if(full) {
    for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
      GcVerifier::CheckObjectAfter(*iter, true);
    }
  }

  GcVerifier::Finish(start);
  GcVerifier::AdjustPeriod();
}

// Invariant C, called from fixup (under allocated_lock: takes no lock) for a typed
// slot ForwardedAddr could not forward
void MemoryManager::VerifyUnforwarded(size_t* value)
{
  if(GcVerifier::IsYoungObject((size_t)value)) {
    GcVerifier::phase = L"fixup";
    const Origin origin = { nullptr, nullptr, L"slot", -1, true };
    GcVerifier::Report(L"C", origin, (size_t)value, L"typed reference to young '" + GcVerifier::Describe(value) +
                       L"' was not forwarded: the object was not marked (missed root)");
    GcVerifier::Fail();
  }
}

// OBJECK_GC_VERIFY_INJECT=barrier: once per collection epoch, the first old object
// whose write barrier reaches the slow path is left out of the remembered set
bool MemoryManager::VerifySkipBarrier(size_t* target_obj)
{
  const long epoch = GcVerifier::collection.load(std::memory_order_relaxed);
  long seen = GcVerifier::barrier_skip_epoch.load(std::memory_order_relaxed);
  if(seen != epoch) {
    if(!GcVerifier::barrier_skip_epoch.compare_exchange_strong(seen, epoch)) {
      return false;
    }
    GcVerifier::barrier_skip_target.store(target_obj);
    if(!GcVerifier::barrier_noted.exchange(true)) {
      std::wcerr << L"[gc-verify] inject=barrier: skipping the write barrier for "
                 << GcVerifier::Hex((size_t)target_obj) << L" once per collection" << std::endl;
    }
    return true;
  }
  return GcVerifier::barrier_skip_target.load() == target_obj;
}
