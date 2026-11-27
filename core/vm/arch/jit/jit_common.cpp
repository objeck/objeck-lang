/***************************************************************************
 * Common JIT compiler functions
 *
 * Copyright (c) 2026 Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list  of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, RXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, RVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include "jit_common.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _NO_JIT
#if defined(_M_ARM64)
#include "arm64/jit_arm_a64.h"
#elif defined(_WIN64) || defined(_X64)
#include "amd64/jit_amd_lp64.h"
#else
#include "arm64/jit_arm_a64.h"
#endif
#endif

StackProgram* JitCompiler::program;

// The call sites of the loaded program, by the method each names: what
// PatchCallSites rewrites when that method's native code is published.
//
// PatchCallSites used to find them by walking every instruction of every
// method of the program, once per compiled method. Starting a small MCP server
// (a 641 KB image) compiles 53 methods before its first answer, and those 53
// walks were 26 M of the run's 150 M instructions -- more than generating the
// code. The walk that fills the table is made once, when the second method is
// compiled, and each later call visits only its own sites. The first method
// compiled is patched by a plain walk as before: a program that compiles one
// method (hello world compiles String->Append and nothing else) would pay for a
// table it never reads again.
//
// The table holds, for a (class id, method id) pair, the instructions the walk
// would have matched, in the order it would have reached them: every MTHD_CALL
// (and MTHD_CALL_JIT) whose first two operands are that pair. Nothing changes
// those operands after the program is loaded, and the only change to such an
// instruction's type is the MTHD_CALL -> MTHD_CALL_JIT rewrite below, which the
// walk treated as the same site; so the table built once is the set any later
// walk would find. OBJECK_JIT_PATCH_VERIFY=1 checks exactly that on every
// call: it makes the walk as well and stops the VM if the two differ. (The
// walk and the table share IsCallSite, so the check covers the table's
// bookkeeping -- a site missed, duplicated or out of order -- and not which
// opcodes count as call sites; that is the one decision below.)
//
// Threads: PatchCallSites holds call_site_mutex from its first read of the
// program to its last store into it, so one thread at a time walks the
// program, builds the table or rewrites a call site. The walk and the build
// read each instruction's type, which a patch rewrites (MTHD_CALL ->
// MTHD_CALL_JIT); before the lock covered the whole function, the first
// method's patch -- the one that runs without the table -- and a second
// thread's walk or build could run together, one storing the field the other
// was reading. The build took either value as a call site, so nothing could go
// wrong on the machines this runs on, but it was a data race the lock removes
// for the price of one uncontended lock per compiled method. The table itself
// is never changed once published. Initialize (program start, and each
// debugger restart) drops the table with the program it described; no method
// is being compiled then.
//
// The lock is a reader-writer lock, and PatchCallSites is its only writer.
// The readers are the code that reads opcodes of methods it is not patching:
// a backend compiling a method (TryAutoJitCompile holds the lock, shared,
// around Compile) and HasLoop. A backend reads every opcode of the method it
// compiles, several times, and that method's call sites belong to callees
// that another thread may finish compiling at any moment: the callee's patch
// stored the caller's opcode while the caller's backend was reading it. Both
// opcodes compile to the same call, so no wrong code came of it, but it was a
// data race in every pair of threads that compiled a caller and its callee
// together (ThreadSanitizer: one report per pair). Compiles still run side by
// side; a patch waits for the compiles in flight, which are milliseconds, and
// is itself a handful of stores.
//
// What the lock does not cover, and need not: the interpreter executing a
// call site while it is patched. See PatchCallSites and
// docs/design/JIT_CALL_SITE_PATCHING.md.
namespace {
  // the two operands exactly as the walk compared them
  typedef std::pair<long, long> CallSiteKey;
  struct CallSiteKeyHash {
    size_t operator()(const CallSiteKey& key) const {
      return std::hash<long>()(key.first) * 1000003u ^ std::hash<long>()(key.second);
    }
  };
  typedef std::unordered_map<CallSiteKey, std::vector<StackInstr*>, CallSiteKeyHash> CallSiteTable;

  std::shared_mutex call_site_mutex;
  std::atomic<const CallSiteTable*> call_site_table(nullptr);
  // PatchCallSites calls since Initialize; the first is served by a walk
  std::atomic<long> call_site_patches(0);

  inline bool IsCallSite(const InstructionType type) {
    // Only CALL instructions carry (class id, method id) in operand/operand2.
    // This used to test the operands alone, so any other opcode whose first
    // two operands happened to match -- a JMP_TABLE (base, range), say -- had
    // its operand3 overwritten. For JMP_TABLE operand3 is default_ip: the
    // default arm of a select was retargeted to instruction 1.
    //
    // And of the calls, only the direct ones. A DYN_MTHD_CALL's first two
    // operands are the words its arguments take and its return type's code
    // (Loader: StackInstr(line, DYN_MTHD_CALL, num_params, rtrn_type)); its
    // target is the function value on the stack. Counted as a call site, a
    // function-value call taking one word and returning an Int was "a call to"
    // whatever method has class id 1 and method id INT_TYPE, and compiling
    // that unrelated method rewrote the site to DYN_MTHD_CALL_JIT with
    // operand3 = 1: from then on the interpreter compiled every function it
    // called through the site on its first call there, whatever the auto-JIT
    // threshold said. A dynamic call is dispatched on its resolved callee's
    // own state instead (StackInterpreter::ProcessDynamicMethodCall).
    return type == MTHD_CALL || type == MTHD_CALL_JIT;
  }

  // every call site of the program, in class, method and instruction order
  template<typename Visit> void WalkCallSites(StackProgram* prgm, Visit visit) {
    const long cls_num = prgm->GetClassNumber();
    StackClass** classes = prgm->GetClasses();
    for(long c = 0; c < cls_num; ++c) {
      StackClass* cls = classes[c];
      if(!cls) continue;

      const int mthd_num = cls->GetMethodCount();
      StackMethod** methods = cls->GetMethods();
      for(int m = 0; m < mthd_num; ++m) {
        StackMethod* method = methods[m];
        if(!method) continue;

        const int instr_count = static_cast<int>(method->GetInstructionCount());
        for(int i = 0; i < instr_count; ++i) {
          StackInstr* instr = method->GetInstruction(i);
          if(IsCallSite(instr->GetType())) {
            visit(instr);
          }
        }
      }
    }
  }

  // the caller holds call_site_mutex
  inline const CallSiteTable* GetCallSiteTable(StackProgram* prgm) {
    const CallSiteTable* table = call_site_table.load(std::memory_order_acquire);
    if(!table) {
      CallSiteTable* built = new CallSiteTable;
      WalkCallSites(prgm, [built](StackInstr* instr) {
        (*built)[CallSiteKey(instr->GetOperand(), instr->GetOperand2())].push_back(instr);
      });
      call_site_table.store(built, std::memory_order_release);
      table = built;
    }

    return table;
  }

  inline bool PatchVerifyEnabled() {
    static const bool enabled = JitEnvFlag("OBJECK_JIT_PATCH_VERIFY");
    return enabled;
  }
}

void JitCompiler::Initialize(StackProgram* p)
{
  program = p;
  // the table describes one program's instructions
  std::lock_guard<std::shared_mutex> lock(call_site_mutex);
  delete call_site_table.exchange(nullptr, std::memory_order_acq_rel);
  call_site_patches.store(0, std::memory_order_release);
}

/**
 * Whether the method holds a back-edge (see the header). Kept out of line: its
 * one hot caller is the interpreter's CheckAutoJit, which runs for every
 * interpreted call of an unpatched site and reaches this only on a callee's
 * first call. Inlined there (by link-time optimisation), the lock's registers
 * were saved and restored on every one of those calls: four instructions on
 * each interpreted call under --jit=off, +0.46% on binarytrees.
 */
#ifdef _MSC_VER
__declspec(noinline) bool JitCompiler::HasLoop(StackMethod* mthd)
#else
bool __attribute__((noinline)) JitCompiler::HasLoop(StackMethod* mthd)
#endif
{
  // the method's call sites may be patched meanwhile: read as a compile does
  std::shared_lock<std::shared_mutex> reading(call_site_mutex);
  for(long i = 0; i < mthd->GetInstructionCount(); ++i) {
    StackInstr* instr = mthd->GetInstruction(i);
    if(instr->GetType() == JMP && instr->GetOperand() <= i) {
      return true;
    }
  }
  return false;
}

JitCompiler::JitCompiler()
{

}

JitCompiler::~JitCompiler()
{

}

#ifndef _NO_JIT
// Blocks until the thread that claimed `callee`'s compile has settled it (done or
// failed). A compile is milliseconds; the bound only keeps a wedged compiler from
// hanging every other thread, which then interprets as it always did. Waiting
// without polling the collector's safepoint is deliberate and safe: the compiler
// thread never parks (Compile() runs no interpreter code and allocates nothing
// on the collected heap), so a collection that is waiting for this thread to park
// waits at most for the compile.
void JitCompiler::WaitForCompile(StackMethod* callee)
{
  if(callee->GetJitState() != StackMethod::JIT_COMPILING) {
    return;
  }

  static const bool report = JitEnvFlag("OBJECK_JIT_REPORT");

  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  const std::chrono::seconds limit(10);
  int spins = 0;
  while(callee->GetJitState() == StackMethod::JIT_COMPILING) {
    if(++spins < 200) {
      std::this_thread::yield();
    }
    else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      if(std::chrono::steady_clock::now() - start > limit) {
        break;
      }
    }
  }

  if(report) {
    const long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::wcerr << L"[jit] " << callee->GetName() << L": waited " << us << L" us for another thread's compile" << std::endl;
  }
}

bool JitCompiler::TryAutoJitCompile(StackMethod* callee)
{
  if(callee->GetNativeCode()) {
    return true;
  }

  // --jit=off / OBJECK_JIT_DISABLE=1 runs the interpreter only. The disabled
  // threshold kept the call counter from ever reaching a compile, but a method
  // declared `native` -- every library method so marked, too -- is compiled on
  // its first call whatever the threshold, so "off" still ran compiled code and
  // an interpreter-vs-JIT comparison (the nightly differential's s0/off) was not
  // one. Every compile comes through here, so this is the one place to refuse.
  // Callers already interpret a method this declines. jit_off_interprets_native
  // fails without it.
  if(JIT_AUTO_THRESHOLD == JIT_AUTO_THRESHOLD_DISABLED) {
    return false;
  }

  // Fast path on the shared compile state (avoids re-claiming after the outcome
  // is settled). A method must be compiled by exactly ONE thread: concurrent
  // mutators previously raced here, double-compiling (leaked NativeCode) and
  // double-patching call sites.
  const int state = callee->GetJitState();
  if(state == StackMethod::JIT_DONE) {
    return true;
  }
  if(state == StackMethod::JIT_FAILED) {
    return false;
  }
  if(!callee->ClaimJitCompile()) {
    // Another thread owns (or just settled) the compile. If it is still in
    // flight, wait for it: a caller that returns now finds no native code and
    // interprets THIS CALL to the end, and compiled code is only picked up at
    // the next call (no on-stack replacement). For a method hot only through a
    // loop inside its first call -- a thread's Run, a kernel -- there is no next
    // call, so the thread that lost the race ran the whole loop ~20x slower.
    WaitForCompile(callee);
    // Re-read the outcome: treat anything but a definitive failure as "will be
    // available" so the caller does not mark this call site as failed.
    return callee->GetJitState() != StackMethod::JIT_FAILED;
  }

  // We own the compile. P2: DYN_MTHD_CALL auto-JIT enabled (func-ref/closure calls).
#if defined(_M_ARM64)
  Runtime::JitArm64 jit_compiler;
#elif defined(_WIN64) || defined(_X64)
  Runtime::JitAmd64 jit_compiler;
#else
  Runtime::JitArm64 jit_compiler;
#endif

  // The backend reads the method's opcodes with no patch in progress (see the
  // call-site table's comment). Released before PatchCallSites, which takes
  // the lock exclusively: Compile calls neither it nor this function.
  bool compiled;
  {
    std::shared_lock<std::shared_mutex> reading(call_site_mutex);
    compiled = jit_compiler.Compile(callee);
  }

  if(compiled) {
    // Compile() published native_code (release). Patch all call sites AFTER that
    // so any thread observing a *_JIT opcode / operand3>0 also sees native_code;
    // the interpreter's JIT paths additionally re-check GetNativeCode() and fall
    // back to interpreting if a patch is observed before the pointer is visible.
    PatchCallSites(callee, 1);
    callee->SetJitDone();
    // Positive evidence for OBJECK_JIT_REPORT readers (tools/fuzz): without a
    // line per compiled method, a VM that never compiles anything prints no
    // rejections either and looks fully covered. Both backends come through here.
    static const bool report = JitEnvFlag("OBJECK_JIT_REPORT");
    if(report) {
      std::wcerr << L"[jit] " << callee->GetName() << L": compiled" << std::endl;
    }
    return true;
  }

  // Definitive failure: mark so every call site can switch to interpret-only.
  callee->SetJitAttempted();
  return false;
}

void JitCompiler::PatchCallSites(StackMethod* callee, long patch_value)
{
  const long target_cls_id = callee->GetClass()->GetId();
  const long target_mthd_id = callee->GetId();

  // one walk, table build or patch at a time (see the table's comment)
  std::unique_lock<std::shared_mutex> lock(call_site_mutex);

  // the first method compiled: its sites straight from a walk (see the table's comment)
  std::vector<StackInstr*> first_sites;
  const bool first = call_site_patches.fetch_add(1, std::memory_order_acq_rel) == 0 &&
                     !call_site_table.load(std::memory_order_acquire);
  if(first) {
    WalkCallSites(program, [&](StackInstr* instr) {
      if(instr->GetOperand() == target_cls_id && instr->GetOperand2() == target_mthd_id) {
        first_sites.push_back(instr);
      }
    });
  }

  static const std::vector<StackInstr*> none;
  const CallSiteTable* table = first && !PatchVerifyEnabled() ? nullptr : GetCallSiteTable(program);
  const std::vector<StackInstr*>* indexed = &none;
  if(table) {
    const CallSiteTable::const_iterator found = table->find(CallSiteKey(target_cls_id, target_mthd_id));
    if(found != table->end()) {
      indexed = &found->second;
    }
  }
  const std::vector<StackInstr*>& sites = table ? *indexed : first_sites;

  // OBJECK_JIT_PATCH_VERIFY=1: the walk this table replaced must find the same
  // sites in the same order
  if(PatchVerifyEnabled()) {
    std::vector<StackInstr*> walked;
    WalkCallSites(program, [&](StackInstr* instr) {
      if(instr->GetOperand() == target_cls_id && instr->GetOperand2() == target_mthd_id) {
        walked.push_back(instr);
      }
    });
    // and no dynamic call site has been written since the program was loaded:
    // its operands name no method, so neither a patch nor the auto-JIT's
    // "refused" mark belongs on it (operand3 is 0 as the loader left it)
    long dynamic_written = 0;
    const long cls_num = program->GetClassNumber();
    for(long c = 0; c < cls_num; ++c) {
      StackClass* cls = program->GetClasses()[c];
      for(int m = 0; cls && m < cls->GetMethodCount(); ++m) {
        StackMethod* method = cls->GetMethods()[m];
        for(long i = 0; method && i < method->GetInstructionCount(); ++i) {
          StackInstr* instr = method->GetInstruction(i);
          if(instr->GetType() == DYN_MTHD_CALL && instr->GetOperand3() != 0) {
            ++dynamic_written;
          }
        }
      }
    }
    if(dynamic_written) {
      lock.unlock();
      std::wcerr << L">>> Internal error: " << dynamic_written << L" dynamic call site(s) were written to; found while patching '"
                 << callee->GetName() << L"' <<<" << std::endl;
      VmExit(1);
    }

    if(walked != sites) {
      // not held through the exit, which destroys the mutex
      lock.unlock();
      std::wcerr << L">>> Internal error: call-site table and program walk differ for '" << callee->GetName()
                 << L"': " << sites.size() << L" indexed, " << walked.size() << L" walked <<<" << std::endl;
      VmExit(1);
    }
  }

  // The stores below race, formally, with interpreters executing these call
  // sites on other threads: `type` and `operand3` are plain words (StackInstr
  // in common.h says why they are not atomics: +1.5% on the interpreted
  // benchmarks, measured). What makes that safe, on x64's ordering and on
  // ARM64's weaker one alike:
  //   - a site's class and method operands are written by the loader before
  //     any thread runs and never again, so there is no "new opcode, old
  //     operands" to observe: MTHD_CALL_JIT reads the operands MTHD_CALL read;
  //   - each store is one aligned word (a 4-byte opcode, a `long`) holding a
  //     value that is valid by itself, and no reader needs the two to agree:
  //     MTHD_CALL with operand3 0 or 1, and MTHD_CALL_JIT with either, all
  //     end in ProcessJitMethodCall or the interpreter for the same callee;
  //   - the one thing a reader needs before it runs native code is the code
  //     pointer, and that is not taken from the site: ProcessJitMethodCall
  //     loads the callee's native_code (acquire; published with release by
  //     Compile before this function is called). ARM64 may show another core
  //     the opcode store before the pointer store. That reader finds no code,
  //     asks TryAutoJitCompile, which waits out this compile (the method is
  //     JIT_COMPILING until the patch is done) and then reads the pointer
  //     again, or interprets the call. A stale read costs a slower call, never
  //     a wrong one.
  for(size_t i = 0; i < sites.size(); ++i) {
    StackInstr* instr = sites[i];
    if(patch_value > 0) {
      // JIT success: rewrite opcode for zero-branch dispatch
      if(instr->GetType() == MTHD_CALL) {
        instr->SetType(MTHD_CALL_JIT);
      }
    }
    instr->SetOperand3(patch_value);
  }
}
#endif

#ifndef _NO_JIT
/**
 * The compiled-to-compiled path (see the header). The direct path used to be
 * inline in JitStackCallback; it is the same code with the callee handed in.
 */
bool JitCompiler::CallCompiled(StackMethod* callee, const bool is_dynamic, [[maybe_unused]] const long cls_id, [[maybe_unused]] const long mthd_id,
                               size_t* op_stack, size_t* stack_pos, StackFrame** call_stack, long* call_stack_pos,
                               int64_t& status)
{
  status = 0;
  // a `virtual` declaration has no body to run; the bridge resolves it for
  // every receiver but Nil, and Nil is the interpreter's to report
  if(callee->IsVirtual()) {
    return false;
  }

  // Auto-JIT: compile hot callees from JIT code (no counting overhead)
  if(!callee->GetNativeCode() && callee->GetJitCallCount() >= JIT_AUTO_THRESHOLD) {
    TryAutoJitCompile(callee);
  }

  // Direct JIT-to-JIT calling: if callee has native code, call it directly
  // without going through the interpreter as a trampoline. This eliminates
  // frame creation + instruction dispatch + MTHD_CALL handler overhead.
  if(!callee->GetNativeCode()) {
    return false;
  }

  // Room for the call, before anything is popped. This direct path bypasses
  // PushFrame, so it checks what PushFrame checks -- the frame slot, committing
  // more of the call stack below the maximum -- and the native stack the
  // compiled callee runs on (call_stack.h). It used to print and exit on a full
  // call stack. Now it declines, and the interpreter the bridge starts for a
  // declined call re-executes the call: its PushFrame fails at the maximum, or
  // its call into compiled code finds the native stack low, and either one
  // reports the full call stack or recovers into a Try() as every other
  // runtime error does (#900).
  Runtime::CallStack* stack = Runtime::CallStack::FromPos(call_stack_pos);
  if((*call_stack_pos) >= stack->depth_limit && !stack->Grow(*stack_pos)) {
    return false;
  }
  if(stack->NativeStackLow()) {
    return false;
  }

  if(is_dynamic) { --(*stack_pos); }   // discard func-ref word; instance below
  // Pop instance from op_stack (same as ProcessMethodCall)
  size_t* callee_inst = (size_t*)op_stack[--(*stack_pos)];

  // Get a stack frame for the callee and register it on the call stack
  // so the GC can find and fixup pointers during young-gen promotion.
  // Uses same convention as PushFrame: store at pos, fence, then increment.
  StackFrame* frame = Runtime::StackInterpreter::GetStackFrame(callee, callee_inst);
  call_stack[(*call_stack_pos)] = frame;
  std::atomic_thread_fence(std::memory_order_release);
  (*call_stack_pos)++;

  // Execute native code directly
  Runtime::JitRuntime jit_executor;
  status = jit_executor.Execute(callee, callee_inst, op_stack, stack_pos,
                                call_stack, call_stack_pos, frame);

  // Unregister and release frame
  (*call_stack_pos)--;
  Runtime::StackInterpreter::ReleaseStackFrame(frame);

  // The callee's status goes OUT, not to an exit (#925 gap 3). The failing
  // frame has already returned and the handler stack is intact, so a Try()
  // below can take it -- and the emitted fast path for the same call does the
  // same thing, which is what keeps the two from disagreeing.
  return true;
}
#endif

/**
 * A call site's inline-cache miss (see the header). Misses are rare -- the
 * first call at a site for each key -- so a spin on the site's flag
 * serializes the fill; hits never take it. FillSiteRecord reuses the key's
 * record when it exists (target unused), else fills a free one.
 */
JitVirtualRecord* JitCompiler::FillSiteRecord(JitVirtualSite* site, size_t key, StackMethod* target)
{
  while(site->filling.exchange(true, std::memory_order_acquire)) {
    ;
  }
  JitVirtualRecord* found = nullptr;
  for(int i = 0; i < site->used; ++i) {
    if(site->records[i].key == key) {
      found = &site->records[i];
      break;
    }
  }
  if(!found && site->used < JitVirtualSite::RECORDS) {
    void* entry = (!target || target->IsVirtual()) ? nullptr : target->GetNativeEntry();
    if(entry) {
      JitVirtualRecord& record = site->records[site->used];
      record.key = key;
      record.entry = entry;
      record.target = target;
      site->used++;
      found = &record;
    }
  }
  if(found) {
    site->current.store(found, std::memory_order_release);
  }
  site->filling.store(false, std::memory_order_release);
  return found;
}

JitVirtualRecord* JitCompiler::JitResolveVirtualSite(JitVirtualSite* site, size_t* receiver)
{
  StackClass* cls = MemoryManager::GetClass(receiver);
  if(!cls) {
    return nullptr;
  }
  // the record can answer without resolving again; resolve only for a new class
  for(int i = 0; i < site->used; ++i) {
    if(site->records[i].key == (size_t)cls) {
      return FillSiteRecord(site, (size_t)cls, nullptr);
    }
  }
  StackMethod* target = Runtime::StackInterpreter::ResolveVirtualTarget(cls, site->declaration, site->decl_cls_id, site->decl_mthd_id);
  return FillSiteRecord(site, (size_t)cls, target);
}

/**
 * A func-ref call site's miss: the packed word names the target directly.
 */
JitVirtualRecord* JitCompiler::JitResolveFuncRefSite(JitVirtualSite* site, size_t packed)
{
  // an unassigned function variable: no record, so the call takes the slow
  // path, where the interpreter reports it (#1183)
  if(!packed) {
    return nullptr;
  }

  const long cls_id = (long)((packed >> 16) & 0xFFFF);
  const long mthd_id = (long)(packed & 0xFFFF);
  StackClass* cls = program->GetClass(cls_id);
  StackMethod* target = cls ? cls->GetMethod(mthd_id) : nullptr;
  return FillSiteRecord(site, packed, target);
}

/**
 * The direct bridge entry (see the header): a native call site's slow path
 * for a callee not compiled yet, and the whole path for a callee whose
 * func-ref result needs two words. The trampoline is the one
 * JitStackCallback takes: the caller's MTHD_CALL re-executes in the
 * interpreter, which is where the auto-JIT counts the callee's calls.
 */
int64_t JitCompiler::JitDirectCall(StackMethod* callee, [[maybe_unused]] StackInstr* instr, const long cls_id,
                                const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                                StackFrame** call_stack, long* call_stack_pos, const long ip)
{
  try {
#ifndef _NO_JIT
    int64_t status = 0;
    if(CallCompiled(callee, false, cls_id, mthd_id, op_stack, stack_pos, call_stack, call_stack_pos, status)) {
      return status;
    }
#endif
    Runtime::StackInterpreter intpr(call_stack, call_stack_pos);
    intpr.Execute(op_stack, stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), inst, true);
    // the interpreted callee failed under a Try() beneath this compiled frame
    return intpr.BridgeDeferred() ? JIT_STATUS_DEFERRED : 0;
  }
  catch(const std::bad_alloc&) {
    BridgeExceptionExit(nullptr, true, cls_id, mthd_id, call_stack, call_stack_pos);
  }
  catch(const std::exception& e) {
    BridgeExceptionExit(e.what(), false, cls_id, mthd_id, call_stack, call_stack_pos);
  }
  catch(...) {
    BridgeExceptionExit(nullptr, false, cls_id, mthd_id, call_stack, call_stack_pos);
  }
  // Unreachable: every catch above ends the process. Stated so the signature
  // does not depend on the compiler agreeing about that.
  return 0;
}

/**
 * How StackCallbackBody hands a Nil operand to JitStackCallback when a handler
 * is active (see its LOAD_ARY_SIZE case). Thrown, not returned: with a
 * `return` at those fifteen sites GCC allocated the function's registers
 * differently and every callback, whatever its opcode, ran three instructions
 * longer (callgrind, Linux x64: a cast 164 -> 167, Map->Insert +0.85%). The
 * sites are error paths and the catch is one frame up, in the bridge entry
 * that already catches everything else, so the throw costs nothing until a
 * program clears a Nil array: with it the callbacks run instruction for
 * instruction what they ran before.
 */
namespace {
  struct JitNilOperand {
  };
}

/**
 * JIT machine code callback: the entry compiled code calls for an opcode it
 * hands to the runtime. The switch is StackCallbackBody; this runs it under
 * the catch the header describes, so an exception never reaches the compiled
 * frame below. A nested bridge (compiled code called from here) catches its own.
 */
int64_t JitCompiler::JitStackCallback(const long instr_id, StackInstr* instr, const long cls_id,
                                   const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                                   StackFrame** call_stack, long* call_stack_pos, const long ip)
{
  try {
    // Returned, not exited. Compiled code tests this and leaves through its
    // epilogue with the status in hand, exactly as a guard stub does, and the
    // interpreter's bridge recovers from it if a Try() is active -- which is
    // the whole of #925. Unguarded, the bridge reports and exits as before.
    return StackCallbackBody(instr_id, instr, cls_id, mthd_id, inst, op_stack,
                             stack_pos, call_stack, call_stack_pos, ip);
  }
  catch(const JitNilOperand&) {
    // a Nil array, lock or string under a handler: the guard stubs' status
    return JIT_STATUS_NIL_DEREFERENCE;
  }
  catch(const std::bad_alloc&) {
    BridgeExceptionExit(nullptr, true, cls_id, mthd_id, call_stack, call_stack_pos);
  }
  catch(const std::exception& e) {
    BridgeExceptionExit(e.what(), false, cls_id, mthd_id, call_stack, call_stack_pos);
  }
  catch(...) {
    BridgeExceptionExit(nullptr, false, cls_id, mthd_id, call_stack, call_stack_pos);
  }
  // Unreachable: every catch above ends the process. Stated so the signature
  // does not depend on the compiler agreeing about that.
  return 0;
}

/**
 * The same three lines Execute (vm.cpp) prints for an exception that escapes
 * the interpreter, then the stack from the compiled method down, then exit(1).
 */
void JitCompiler::BridgeExceptionExit(const char* what, const bool out_of_memory, const long cls_id,
                                      const long mthd_id, StackFrame** call_stack, long* call_stack_pos)
{
  if(out_of_memory) {
    std::wcerr << L">>> virtual machine: out of memory <<<" << std::endl;
  }
  else if(what) {
    const std::string msg(what);
    std::wcerr << L">>> virtual machine: internal error: " << std::wstring(msg.begin(), msg.end()) << L" <<<" << std::endl;
  }
  else {
    std::wcerr << L">>> virtual machine: unexpected error <<<" << std::endl;
  }

  // the compiled method's frame is not on the call stack; name it as the top,
  // as the interpreter's own JIT call path does for a guard-stub status
  Runtime::StackInterpreter intpr(call_stack, call_stack_pos);
  intpr.StackErrorUnwind(program->GetClass(cls_id)->GetMethod(mthd_id));
  VmExit(1);
}

/**
 * Pops a new array's dimensions into indices and returns its element count,
 * stopping the VM -- as the interpreter's ProcessNewArray does -- on a negative
 * dimension or a count that leaves no room for 'reserve' more bytes or slots
 * (#1181). Without the checks Byte->New[-1] wrapped the count to 0 and handed
 * back an array whose Size() was -1.
 */
static size_t PopArrayDimensions(StackInstr* instr, size_t* op_stack, size_t* stack_pos,
                                 size_t* indices, long &dim, const size_t reserve)
{
  size_t size = 1;
  dim = 0;
  for(long i = 0; i < instr->GetOperand(); ++i) {
    const size_t value = JitCompiler::PopInt(op_stack, stack_pos);
    if((int64_t)value < 0 || (value != 0 && size > (~(size_t)0) / value)) {
      std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
      VmExit(1);
    }
    size *= value;
    indices[dim++] = value;
  }

  if(size > (~(size_t)0) - reserve) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }

  return size;
}

/**
 * The bridge's opcode switch (see JitStackCallback)
 */
int64_t JitCompiler::StackCallbackBody(const long instr_id, StackInstr* instr, const long cls_id,
                                    const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                                    StackFrame** call_stack, long* call_stack_pos, const long ip)
{
#ifdef _DEBUG_JIT
  std::wcout << L"Stack Call: instr=" << instr_id
    << L", oper_1=" << instr->GetOperand() << L", oper_2=" << instr->GetOperand2()
    << L", oper_3=" << instr->GetOperand3() << L", self=" << inst << L"("
    << (size_t)inst << L"), std::stack=" << op_stack << L", stack_addr=" << stack_pos
    << L", stack_pos=" << (*stack_pos) << std::endl;
#endif
  switch(instr_id) {
  case MTHD_CALL:
  case DYN_MTHD_CALL: {
    // For DYN_MTHD_CALL the target is the packed func-ref word on the op stack
    // (cls<<16|mthd), NOT the instr operands (param-count/return-type).
    StackMethod* callee;
    if(instr_id == DYN_MTHD_CALL) {
      const size_t fref = op_stack[(*stack_pos) - 1];
      // 0 is an unassigned function variable (see FUNC_VALUE_TAG): no callee;
      // the interpreter below reports it, or recovers it under a Try() (#1183)
      // (a class id that names no class: no callee either, where this read a null class)
      StackClass* fref_cls = fref ? program->GetClass((long)((fref >> 16) & 0xFFFF)) : nullptr;
      callee = fref_cls ? fref_cls->GetMethod((long)(fref & 0xFFFF)) : nullptr;
    }
    else {
      callee = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
      // A `virtual` declaration is never the method that runs. Resolve it
      // through the receiver's class (the top operand-stack word), as the
      // interpreter does, so the compiled path can take the override. The
      // lookup used to stop at the declaration, which has no native code and
      // never gets any -- the auto-JIT counts concrete targets only -- so
      // every virtual call from compiled code went through the interpreter
      // trampoline below: 126 ns against the interpreter's own 66 ns. That
      // missing count was also all that kept the compiled path from running
      // the declaration's empty body; CallCompiled now refuses one. A Nil
      // receiver falls through to the interpreter, which reports it.
      if(callee->IsVirtual()) {
        StackClass* receiver_cls = MemoryManager::GetClass((size_t*)op_stack[(*stack_pos) - 1]);
        if(receiver_cls) {
          callee = Runtime::StackInterpreter::ResolveVirtualTarget(receiver_cls, callee, instr->GetOperand(), instr->GetOperand2());
        }
      }
    }

#ifndef _NO_JIT
    int64_t called_status = 0;
    if(callee && CallCompiled(callee, instr_id == DYN_MTHD_CALL, cls_id, mthd_id, op_stack, stack_pos,
                              call_stack, call_stack_pos, called_status)) {
      if(called_status < 0) {
        return called_status;
      }
      break;
    }
#endif

#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: MTHD_CALL: mthd=" << (callee ? callee->GetName() : L"<unassigned>") << std::endl;
#endif
    Runtime::StackInterpreter intpr(call_stack, call_stack_pos);
    intpr.Execute(op_stack, stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), inst, true);
    if(intpr.BridgeDeferred()) {
      return JIT_STATUS_DEFERRED;
    }
  }
    break;

  case LOAD_ARY_SIZE: {
    size_t* array = (size_t*)PopInt(op_stack, stack_pos);
    if(!array) {
      // Under a handler -- a ?-> region, or the sentinel of an isolated thread --
      // a Nil operand leaves compiled code as the guard stubs' status for a Nil
      // dereference (JitNilOperand, which JitStackCallback turns into the
      // status), here and in every case below that is handed Nil: the string
      // conversions, the thread and lock operations, the four array copies and
      // the four array clears. Each printed the error and ended the process
      // whatever was active, where the interpreter's handler for the same
      // instruction goes through TryErrorRecovery: a ?-> region recovered from
      // Byte->Clear(Nil) under --jit=off and the program died once the caller
      // was compiled, and with thread-fault isolation on a worker took the
      // process with it. The interpreter that owns the handler records the
      // status and recovers (RecordRecoveredStatus), as it does for a guard
      // stub's. With no handler the report and the exit below are unchanged.
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    PushInt(op_stack, stack_pos, array[2]);
  }
    break;

  case RAND_FLOAT:
    PushFloat(MemoryManager::GetRandomValue(), op_stack, stack_pos);
    break;

  case NEW_BYTE_ARY: {
    size_t indices[8];
    long dim;
    size_t size = PopArrayDimensions(instr, op_stack, stack_pos, indices, dim, 1 + (8 + 2) * sizeof(size_t));

    // One byte of NUL padding in the ALLOCATION only. [0] is the element
    // count -- as the interpreter's ProcessNewByteArray, NEW_CHAR_ARY below and
    // APITools_MakeByteArray all write it -- because native libraries read
    // [0] as the size. This wrote count + 1, so a Byte[] built in a native
    // method carried a phantom trailing zero into every native call:
    // Hash->SHA256 of a 3-byte array digested 4 bytes.
    size++;
    size_t* mem = MemoryManager::AllocateArray((int64_t)(size + ((dim + 2) * sizeof(size_t))), BYTE_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size - 1;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);

#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_BYTE_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << std::endl;
#endif
  }
    break;

  case NEW_CHAR_ARY: {
    size_t indices[8];
    long dim;
    size_t size = PopArrayDimensions(instr, op_stack, stack_pos, indices, dim, 1 + (8 + 2) * sizeof(size_t));

    size++;
    size_t* mem = (size_t*)MemoryManager::AllocateArray((int64_t)(size + ((dim + 2) * sizeof(size_t))), CHAR_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size - 1;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);

#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_CHAR_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << std::endl;
#endif
  }
    break;

  case NEW_INT_ARY:
  case NEW_OBJ_ARY: {
    size_t indices[8];
    long dim;
    const size_t size = PopArrayDimensions(instr, op_stack, stack_pos, indices, dim, 8 + 2);

    // an Int[] is INT_ARY_TYPE, which the collector never scans; an array of
    // references is INT_TYPE (GC roots phase 6)
    const MemoryType elem_type = instr->GetType() == NEW_INT_ARY ? INT_ARY_TYPE : INT_TYPE;
    size_t* mem = (size_t*)MemoryManager::AllocateArray((int64_t)(size + dim + 2), elem_type, op_stack, *stack_pos);
#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_INT_ARY/NEW_OBJ_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << std::endl;
#endif
    mem[0] = size;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);
  }
    break;

  case NEW_FLOAT_ARY: {
    size_t indices[8];
    long dim;
    const size_t size = PopArrayDimensions(instr, op_stack, stack_pos, indices, dim, 8 + 2);

    size_t* mem = (size_t*)MemoryManager::AllocateArray((int64_t)(size + dim + 2), FLOAT_TYPE, op_stack, *stack_pos);
    mem[0] = size;
    mem[1] = dim;

    memcpy(mem + 2, indices, dim * sizeof(size_t));
    PushInt(op_stack, stack_pos, (size_t)mem);
  }
    break;

  case NEW_OBJ_INST: {
#ifdef _DEBUG_JIT
    StackClass* klass = program->GetClass(instr->GetOperand());
    std::wcout << L"jit oper: NEW_OBJ_INST: class=" << klass->GetName() << std::endl;
#endif
    size_t* mem = MemoryManager::AllocateObject(instr->GetOperand(), op_stack, *stack_pos);
    PushInt(op_stack, stack_pos, (size_t)mem);
  }
    break;

  case I2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const size_t base = PopInt(op_stack, stack_pos);
      const int64_t value = (int64_t)PopInt(op_stack, stack_pos);

      std::wstring conv;
      std::wstringstream formatter;

      const std::wstring int_format = program->GetProperty(L"int:string:format");
      if(!int_format.empty()) {
        if(int_format == L"dec") {
          formatter << std::dec;
        }
        else if(int_format == L"oct") {
          formatter << std::oct;
        }
        else if(int_format == L"hex") {
          formatter << std::hex;
        }

        formatter << value;
        conv = formatter.str();
      }
      else {
        switch(base) {
        case 8:
          formatter << std::oct;
          break;

        case 10:
          formatter << std::dec;
          break;

        case 16:
          formatter << std::hex << L"0x";
          break;
        }

        formatter << value;
        conv = formatter.str();
      }

      // The buffer is the caller's Char[]: str_ptr[0] elements. The text is cut to it and
      // ended with a zero, which for text exactly as long as the buffer is the array's
      // spare element (common.h). The interpreter gave wcsncpy_s the element count as the
      // size, so a value exactly as long as its buffer ("-128" into Char->New[4]) ended
      // the process on Windows, and on POSIX it copied up to 32 characters whatever
      // the buffer held; the JIT's copy was already bounded. One rule for both now.
      const size_t capacity = str_ptr[0];
      StoreCharArrayText(str, conv.c_str(), conv.size() < capacity ? conv.size() : capacity);
    }
  }
    break;

  case F2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);

      std::wstringstream formatter;
      std::wstring conv;

      const std::wstring float_format = program->GetProperty(L"float:string:format");
      const std::wstring float_precision = program->GetProperty(L"float:string:precision");
      // not std::stoll, which threw on a value that was not a number (common.h)
      int precision = 0;
      const bool has_precision = ParseFloatPrecision(float_precision, precision);

      if(!float_format.empty() && has_precision) {
        if(float_format == L"fixed") {
          formatter << std::fixed;
        }
        else if(float_format == L"scientific") {
          formatter << std::scientific;
        }
        else if(float_format == L"hex") {
          formatter << std::hexfloat;
        }
        formatter << std::setprecision(precision);

        formatter << value;
        conv = formatter.str();
      }
      else if(!float_format.empty()) {
        if(float_format == L"fixed") {
          formatter << std::fixed;
        }
        else if(float_format == L"scientific") {
          formatter << std::scientific;
        }
        else if(float_format == L"hex") {
          formatter << std::hexfloat;
        }

        formatter << value;
        conv = formatter.str();
      }
      else if(has_precision) {
        formatter << std::setprecision(precision);

        formatter << value;
        conv = formatter.str();
      }
      else {
        conv = std::to_wstring(value);
      }

      // The buffer is the caller's Char->New[64]: str_ptr[0] elements. Fixed-point
      // text of a large value does not fit -- 1e60 is 61 digits and ".000000" --
      // and copying 64 characters into it left no room for the terminator:
      // MSVC's invalid-parameter handler ended the process with no message, and
      // POSIX left the string unterminated (#1173). Such a value is written in
      // scientific notation instead, and the copy always keeps the terminator.
      const size_t capacity = str_ptr[0];
      if(conv.size() >= capacity) {
        std::wstringstream scientific;
        scientific << std::scientific << value;
        conv = scientific.str();
      }
      // (a zero-length buffer takes the zero on its spare element: wcsncpy_s refused a size
      // of 0 and ended the process)
      const size_t max = conv.size() < capacity ? conv.size() : (capacity > 0 ? capacity - 1 : 0);
      StoreCharArrayText(str, conv.c_str(), max);
    }
  }
    break;

  case S2F: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      // as the interpreter's Str2Float: text that is not a number is 0.0, and
      // out-of-range text is strtod's +/-inf, 0 or subnormal (common.h)
      PushFloat(ParseFloatText(str), op_stack, stack_pos);
    }
    else {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      VmExit(1);
    }
  }
    break;

  case S2I: {
#ifdef _DEBUG_JIT
    std::wcout << L"stack oper: S2I; call_pos=" << (*call_stack_pos) << std::endl;
#endif

    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    const long base = (long)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      // the interpreter's rules, signed prefixes included (common.h)
      PushInt(op_stack, stack_pos, (size_t)ParseIntText((wchar_t*)(str_ptr + 3), static_cast<int>(base)));
    }
    else {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      VmExit(1);
    }
  }
    break;

  case OBJ_TYPE_OF: {
    size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
    size_t* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(), program->GetHierarchy(), program->GetInterfaces());
    if(result) {
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case OBJ_INST_CAST: {
    size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
    long to_id = instr->GetOperand();
#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: OBJ_INST_CAST: from=" << mem << L", to=" << to_id << std::endl;
#endif
    size_t result = (size_t)MemoryManager::ValidObjectCast(mem, to_id, program->GetHierarchy(), program->GetInterfaces());
    if(!result && mem) {
      // Reported, not exited (#925). The message is printed here, where the
      // two class names are still in hand; the caller decides what the status
      // means. Nothing pushes a result on this path -- a recovering Try()
      // resets the operand stack to the handler's saved position, so a value
      // left here would be discarded, and a value pushed for an abort would be
      // read by nobody.
      // Printed only when nothing will catch it. The interpreter recovers from
      // this silently, so printing unconditionally would make the same
      // recovered error noisier in compiled code than in interpreted code --
      // and the two agreeing is the whole of #925. Unguarded, the message is
      // unchanged and names both classes, printed here where they are in hand.
      if(!Runtime::StackInterpreter::ThreadHasTryHandler()) {
        StackClass* to_cls = MemoryManager::GetClass(mem);
        std::wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
          << L"' to '" << program->GetClass(to_id)->GetName() << L"' <<<" << std::endl;
        std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      }
      else {
        // what a Try() will recover from, with both names while they are in
        // hand; the status carries only the kind (#1247)
        StackClass* to_cls = MemoryManager::GetClass(mem);
        Runtime::StackInterpreter::RecordRecoveredError(Runtime::StackInterpreter::RECOVERED_INVALID_CAST,
          std::wstring(L"Invalid object cast: '") + (to_cls ? to_cls->GetName() : L"?") + L"' to '" +
          program->GetClass(to_id)->GetName() + L"'");
      }
      return JIT_STATUS_INVALID_CAST;
    }
    PushInt(op_stack, stack_pos, result);
  }
    break;

  case NEW_FUNC_INST: {
    const long size = (long)instr->GetOperand();
    size_t func_mem = (size_t)MemoryManager::AllocateArray(size, BYTE_ARY_TYPE, op_stack, *stack_pos);
    PushInt(op_stack, stack_pos, func_mem);
#ifdef _DEBUG_JIT
    std::wcout << L"jit oper: NEW_FUNC_INST: size=" << size << L"; mem=" << func_mem << std::endl;
#endif
  }
    break;

    //----------- threads -----------

  case THREAD_JOIN: {
    size_t* instance = inst;
    if(!instance) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }

    // what this Join reports about the thread (thread-fault isolation)
    Runtime::StackInterpreter::JoinedThreadFault().clear();

    // The handle is read BEFORE parking: while parked, another thread's collection
    // can promote this Thread object and leave the local 'instance' pointing into the
    // recycled nursery, so instance[0] would be whatever reused that memory (#874).
#ifdef _WIN32
    // The handle is taken out of the object NOW, while 'instance' is still the live
    // object: after the wait it may have been moved, and no frame of this callback
    // reliably holds the Thread (a bound call reaches here without a frame of its own, so
    // the frame below is the caller's -- writing through it cleared a field of the
    // caller's object and broke TaskScope under --jit=1). A second Join of the thread then
    // finds the slot empty and returns, instead of waiting on, or closing, a handle the
    // system may have reused. Taken in one step with its key, under the registry's lock
    // (TakeThreadHandle): an Execute of the same object on another thread stores a new
    // handle either before this, and is joined here, or after, and is left alone.
    size_t fault_key = 0;
    HANDLE vm_thread = (HANDLE)Runtime::StackInterpreter::TakeThreadHandle(instance, fault_key);
    if(!vm_thread) {
      // already joined, or being joined: that Join took the handle
      break;
    }

    // Joining blocks in a syscall — bracket with Begin/EndBlocking so this thread
    // counts as parked, otherwise a collection on another thread waits forever for
    // it to reach a safepoint (the JIT path previously omitted this — deadlock).
    MemoryManager::BeginBlocking();
    const DWORD wait_result = WaitForSingleObject(vm_thread, INFINITE);
    if(wait_result != WAIT_OBJECT_0) {
      // captured before EndBlocking so nothing can overwrite it (#874)
      const DWORD last_error = GetLastError();
      MemoryManager::EndBlocking();
      std::wcerr << L"Unable to join thread!" << std::endl;
      std::wcerr << L"  wait result=" << wait_result << L", GetLastError=" << last_error
                 << L", handle=0x" << std::hex << (size_t)vm_thread << L", thread object=0x" << (size_t)instance
                 << std::dec << std::endl;
      VmExit(-1);
    }
#else
    void* status;
    // as above: out of the object and with its key in one step
    size_t fault_key = 0;
    pthread_t vm_thread = (pthread_t)Runtime::StackInterpreter::TakeThreadHandle(instance, fault_key);
    if(!vm_thread) {
      // already joined, or being joined: that Join took the handle
      break;
    }

    // see the note above: the handle is read before this thread parks
    MemoryManager::BeginBlocking();
    const int join_result = pthread_join(vm_thread, &status);
    if(join_result) {
      MemoryManager::EndBlocking();
      // the return code is the diagnosis: ESRCH (not a live thread), EINVAL (not
      // joinable, or already joined), EDEADLK (joining itself) (#874)
      std::wcerr << L"Unable to join thread!" << std::endl;
      std::wcerr << L"  pthread_join=" << join_result << L" (" << strerror(join_result) << L"), handle=0x"
                 << std::hex << (size_t)vm_thread << L", thread object=0x" << (size_t)instance << std::dec << std::endl;
      VmExit(-1);
    }
    Runtime::StackInterpreter::TakeThreadFault(fault_key, (size_t)status == Runtime::StackInterpreter::THREAD_FAULT_EXIT);
#ifdef __linux__
    // the stack the VM allocated for it (ProcessAsyncMethodCall)
    Runtime::ThreadStacks::Release(vm_thread);
#endif
#endif
    MemoryManager::EndBlocking();

#ifdef _WIN32
    DWORD exit_code = 0;
    GetExitCodeThread(vm_thread, &exit_code);
    Runtime::StackInterpreter::TakeThreadFault(fault_key, exit_code == Runtime::StackInterpreter::THREAD_FAULT_EXIT);
    // The thread has finished and been joined: close its handle, which this path never
    // did, so a program whose join loop was compiled kept one kernel object per thread it
    // started (the interpreter's THREAD_JOIN closes it; only the first few joins of a
    // loop run there).
    CloseHandle(vm_thread);
#endif
  }
    break;

  case THREAD_SLEEP: {
    const long sleep_ms = (long)PopInt(op_stack, stack_pos);
    MemoryManager::BeginBlocking();   // sleeping blocks — count as parked
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    MemoryManager::EndBlocking();
  }
    break;

  case THREAD_MUTEX: {
    size_t* instance = inst;
    if(!instance) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    ThreadMutexCreate(instance);
  }
    break;

  case CRITICAL_START: {
    size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    ThreadMutexLock(instance);
  }
    break;

  case CRITICAL_END: {
    size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L"Attempting to dereference a 'Nil' memory instance" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    ThreadMutexUnlock(instance);
  }
    break;

    // ---------------- memory copy ----------------
  case CPY_BYTE_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }

    const long src_array_len = (long)src_array[2];
    const long dest_array_len = (long)dest_array[2];
    if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      char* src_array_ptr = (char*)(src_array + 3);
      char* dest_array_ptr = (char*)(dest_array + 3);
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_CHAR_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }

    const long src_array_len = (long)src_array[2];
    const long dest_array_len = (long)dest_array[2];

    if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      const wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
      wchar_t* dest_array_ptr = (wchar_t*)(dest_array + 3);
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_INT_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }

    const long src_array_len = (long)src_array[0];
    const long dest_array_len = (long)dest_array[0];
    if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      size_t* src_array_ptr = src_array + 3;
      size_t* dest_array_ptr = dest_array + 3;
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
      }
      // Generational GC write barrier, mirroring StackInterpreter::CpyIntAry. An
      // Int[] can hold object references (an object array IS an int array of
      // pointers), so a bulk copy can deposit young refs into an old destination
      // array where a minor GC will neither mark nor fix them up. The interpreter
      // path has recorded the destination since the barrier was introduced; this
      // JIT callback performs the same copy and did not.
      //
      // Only the Int variant needs this. CPY_BYTE_ARY, CPY_CHAR_ARY and
      // CPY_FLOAT_ARY move primitives, which are never references.
      MemoryManager::WriteBarrier(dest_array);
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_FLOAT_ARY: {
    long length = (long)PopInt(op_stack, stack_pos);
    const long src_offset = (long)PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = (long)PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }

    const long src_array_len = (long)src_array[0];
    const long dest_array_len = (long)dest_array[0];
    if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      size_t* src_array_ptr = src_array + 3;
      size_t* dest_array_ptr = dest_array + 3;
      if(src_array_ptr == dest_array_ptr) {
        memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
      }
      else {
        memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
      }
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case ZERO_BYTE_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    // clearing a Nil array is a runtime error, as copying from one is above:
    // recoverable under a handler (see LOAD_ARY_SIZE)
    if(!array_ptr) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    const size_t array_len = array_ptr[0];
    char* buffer = (char*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(char));
  }
    break;

  case ZERO_CHAR_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    // clearing a Nil array is a runtime error, as copying from one is above:
    // recoverable under a handler (see LOAD_ARY_SIZE)
    if(!array_ptr) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    const size_t array_len = array_ptr[0];
    wchar_t* buffer = (wchar_t*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(wchar_t));
  }
    break;

  case ZERO_INT_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    // clearing a Nil array is a runtime error, as copying from one is above:
    // recoverable under a handler (see LOAD_ARY_SIZE)
    if(!array_ptr) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    const size_t array_len = array_ptr[0];
    size_t* buffer = (size_t*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(size_t));
  }
    break;

  case ZERO_FLOAT_ARY: {
    size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
    // clearing a Nil array is a runtime error, as copying from one is above:
    // recoverable under a handler (see LOAD_ARY_SIZE)
    if(!array_ptr) {
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        throw JitNilOperand();
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      std::wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << std::endl;
      VmExit(1);
    }
    const size_t array_len = array_ptr[0];
    FLOAT_VALUE* buffer = (FLOAT_VALUE*)(array_ptr + 3);
    memset(buffer, 0, array_len * sizeof(FLOAT_VALUE));
  }
    break;

  case TRAP:
  case TRAP_RTRN:
    if(!TrapProcessor::ProcessTrap(program, inst, op_stack, stack_pos, nullptr)) {
      // under a ?-> handler a failing trap, a raise among them, passes outward
      // as a status like a guard stub's (#1247)
      bool& recorded = Runtime::StackInterpreter::TrapErrorRecorded();
      const bool had_record = recorded;
      recorded = false;
      if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
        if(!had_record) {
          Runtime::StackInterpreter::RecordRecoveredError(Runtime::StackInterpreter::RECOVERED_OTHER, L"A runtime library call failed");
        }
        return JIT_STATUS_TRAP_FAILED;
      }
      std::wcerr << L"  JIT compiled machine code..." << std::endl;
      VmExit(1);
    }
    break;

#ifdef _DEBUG_JIT
  default:
    std::wcerr << L"Unknown callback!" << std::endl;
    break;
#endif
  }

  // Every case that did not report an error falls through to here.
  return 0;
}

/**
 * Pop integer value
 */
size_t JitCompiler::PopInt(size_t* op_stack, size_t* stack_pos) {
  const size_t value = op_stack[--(*stack_pos)];
#ifdef _DEBUG_JIT
  std::wcout << L"\t[pop_i: value=" << (size_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << std::endl;
#endif

  return value;
}

/**
 * Push integer value
 */
void JitCompiler::PushInt(size_t* op_stack, size_t* stack_pos, size_t value) {
  op_stack[(*stack_pos)++] = value;
#ifdef _DEBUG_JIT
  std::wcout << L"\t[push_i: value=" << (size_t*)value << L"(" << value << L")]" << L"; pos=" << (*stack_pos) << std::endl;
#endif
}

/**
 * Pop FLOAT value
 */
FLOAT_VALUE JitCompiler::PopFloat(size_t* op_stack, size_t* stack_pos) {
  (*stack_pos)--;

#ifdef _DEBUG_JIT
  FLOAT_VALUE v = *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
  std::wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << L"]; pos=" << (*stack_pos) << std::endl;
  return v;
#endif

  return *((FLOAT_VALUE*)(&op_stack[(*stack_pos)]));
}

/**
 * Push FLOAT value
 */
void JitCompiler::PushFloat(const FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG_JIT
  std::wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v
    << L"]; call_pos=" << (*stack_pos) << std::endl;
#endif
  * ((FLOAT_VALUE*)(&op_stack[(*stack_pos)])) = v;
  (*stack_pos)++;
}