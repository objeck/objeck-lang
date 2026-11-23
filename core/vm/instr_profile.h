/***************************************************************************
 * Opcode frequency profile of the interpreter (opt-in, compile time).
 *
 * Build the VM with -DOBJECK_INSTR_PROFILE (Linux: make EXTRA_FLAGS=-DOBJECK_INSTR_PROFILE)
 * and every instruction the interpreter dispatches is counted, together with the
 * pairs and triples of instructions that run one after another in the same method.
 * Without the macro this header is empty and the dispatch loop is untouched, so a
 * normal obr pays nothing.
 *
 * Why: the instruction set is a stack machine, so `i += 1` is four dispatches
 * (load, literal, add, store). Which sequences deserve a fused instruction should
 * come from what programs actually execute, not from a guess.
 *
 *   obr --jit=off program.obe          # compiled code is not counted: turn the JIT off
 *   OBJECK_INSTR_PROFILE_OUT=prof.txt obr --jit=off program.obe   (a "%p" in the name becomes the pid)
 *   python tools/perf/instr_profile_report.py prof.txt
 *
 * The profile is written when the process ends normally (exit or return from
 * main); a run that ends through _Exit (a program that exits with live threads)
 * writes nothing. The VM prints numeric opcode ids; the report script reads the
 * names from core/shared/instrs.h, so no name table here can go stale.
 *
 * A pair counts only when the second instruction is the next one in the method's
 * instruction array: that is a sequence a fused instruction could replace. A jump,
 * a call or a return breaks it. Counters are per thread and merged when the thread
 * ends, so the hot path has no lock and no atomic.
 ***************************************************************************/

#ifndef __INSTR_PROFILE_H__
#define __INSTR_PROFILE_H__

#ifdef OBJECK_INSTR_PROFILE

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace InstrProfile {
  inline long GetProcessIdForProfile() {
#ifdef _WIN32
    return (long)_getpid();
#else
    return (long)getpid();
#endif
  }

  // opcodes are below this (END_STMTS is 146); an id at or above it is not counted
  static const int N = 160;
  static const size_t TOP_TRIPLES = 400;

  struct Totals {
    std::mutex lock;
    std::vector<uint64_t> op;
    std::vector<uint64_t> pair;
    std::vector<uint64_t> triple;

    Totals() : op(N, 0), pair((size_t)N * N, 0), triple((size_t)N * N * N, 0) {
    }

    ~Totals() {
      // OBJECK_INSTR_PROFILE_OUT=<file>; a "%p" in it becomes the process id, so
      // a suite that starts a process per test writes one file each
      std::string path = std::getenv("OBJECK_INSTR_PROFILE_OUT") ? std::getenv("OBJECK_INSTR_PROFILE_OUT") : "";
      const size_t at = path.find("%p");
      if(at != std::string::npos) {
        path.replace(at, 2, std::to_string((long)GetProcessIdForProfile()));
      }
      FILE* out = path.empty() ? stderr : std::fopen(path.c_str(), "w");
      if(!out) {
        out = stderr;
      }
      std::fprintf(out, "#instr-profile v1 N=%d\n", N);
      for(int a = 0; a < N; ++a) {
        if(op[a]) {
          std::fprintf(out, "op %d %llu\n", a, (unsigned long long)op[a]);
        }
      }
      for(int a = 0; a < N; ++a) {
        for(int b = 0; b < N; ++b) {
          const uint64_t n = pair[(size_t)a * N + b];
          if(n) {
            std::fprintf(out, "pair %d %d %llu\n", a, b, (unsigned long long)n);
          }
        }
      }
      std::vector<std::pair<uint64_t, uint32_t> > top;
      for(size_t k = 0; k < triple.size(); ++k) {
        if(triple[k]) {
          top.push_back(std::make_pair(triple[k], (uint32_t)k));
        }
      }
      std::sort(top.begin(), top.end(), [](const std::pair<uint64_t, uint32_t>& l, const std::pair<uint64_t, uint32_t>& r) {
        return l.first > r.first;
      });
      if(top.size() > TOP_TRIPLES) {
        top.resize(TOP_TRIPLES);
      }
      for(const auto& t : top) {
        const uint32_t k = t.second;
        std::fprintf(out, "triple %u %u %u %llu\n", k / (N * N), (k / N) % N, k % N, (unsigned long long)t.first);
      }
      if(out != stderr) {
        std::fclose(out);
      }
    }
  };

  inline Totals& Global() {
    static Totals totals;
    return totals;
  }

  struct Local {
    std::vector<uint64_t> op;
    std::vector<uint64_t> pair;
    std::vector<uint64_t> triple;
    const void* base;   // the method's instruction array last counted
    long idx;           // and the index within it
    int p1, p2;         // the previous two opcodes of the current straight-line run, -1 when absent

    Local() : op(N, 0), pair((size_t)N * N, 0), triple((size_t)N * N * N, 0), base(nullptr), idx(-1), p1(-1), p2(-1) {
      Global();   // built first, so it is destroyed after every thread's counters have merged into it
    }

    ~Local() {
      Totals& g = Global();
      std::lock_guard<std::mutex> guard(g.lock);
      for(size_t k = 0; k < op.size(); ++k) g.op[k] += op[k];
      for(size_t k = 0; k < pair.size(); ++k) g.pair[k] += pair[k];
      for(size_t k = 0; k < triple.size(); ++k) g.triple[k] += triple[k];
    }
  };

  inline void Count(const void* base, long idx, int opcode) {
    if(opcode < 0 || opcode >= N) {
      return;
    }
    static thread_local Local local;
    Local& l = local;
    l.op[(size_t)opcode]++;
    if(l.base == base && l.idx + 1 == idx) {
      if(l.p1 >= 0) {
        l.pair[(size_t)l.p1 * N + opcode]++;
        if(l.p2 >= 0) {
          l.triple[((size_t)l.p2 * N + l.p1) * N + opcode]++;
        }
      }
      l.p2 = l.p1;
      l.p1 = opcode;
    }
    else {
      l.p2 = -1;
      l.p1 = opcode;
    }
    l.base = base;
    l.idx = idx;
  }
}

#endif  // OBJECK_INSTR_PROFILE

#endif
