/***************************************************************************
 * Debug-only execution trace for validating the compiler's control-flow graph.
 *
 * Built only with -DOBJECK_CFG_TRACE (nothing is compiled into a release VM). When the
 * environment variable OBJECK_CFG_TRACE names a file, the interpreter records, per method,
 * every instruction index it executes and every transition from one instruction to the
 * next inside one activation, and appends them to that file at exit. A trap taken by a
 * try region is recorded as a transition from the trapping instruction (or the call that
 * unwound to the handler) to the handler. tools/cicd/cfg_oracle.py checks the transitions
 * against the graph the compiler dumped (OBJECK_CFG_DUMP).
 *
 * Run the interpreter only: --jit=off.
 ***************************************************************************/

#pragma once

#ifdef OBJECK_CFG_TRACE

#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CfgTrace {
  struct MethodTrace {
    std::string name;
    long count;
    std::vector<unsigned char> executed;
    std::vector<unsigned char> sequential;   // ip was followed by ip + 1
    std::unordered_set<unsigned long long> edges;
  };

  struct Registry {
    std::mutex lock;
    std::unordered_map<StackMethod*, MethodTrace*> methods;
    std::string path;
    bool flushed = false;
  };

  inline Registry& Reg() {
    static Registry* r = new Registry;
    return *r;
  }

  struct ThreadState {
    StackFrame* frame = nullptr;
    StackMethod* method = nullptr;
    MethodTrace* trace = nullptr;
    long prev = -1;
  };

  inline ThreadState& TS() {
    static thread_local ThreadState ts;
    return ts;
  }

  inline void Flush() {
    Registry& r = Reg();
    std::lock_guard<std::mutex> guard(r.lock);
    if(r.flushed || r.path.empty()) {
      return;
    }
    r.flushed = true;
    // one file per process: a test that runs another program writes its own
    const std::string file = r.path + "." + std::to_string(static_cast<long>(getpid()));
    FILE* out = std::fopen(file.c_str(), "w");
    if(!out) {
      return;
    }
    for(auto& entry : r.methods) {
      MethodTrace* m = entry.second;
      std::fprintf(out, "M %s %ld\n", m->name.c_str(), m->count);
      for(long i = 0; i < m->count; ++i) {
        if(m->executed[i]) {
          std::fprintf(out, "X %ld\n", i);
        }
        if(m->sequential[i]) {
          std::fprintf(out, "F %ld\n", i);
        }
      }
      for(unsigned long long e : m->edges) {
        std::fprintf(out, "E %llu %llu\n", e >> 32, e & 0xffffffffULL);
      }
    }
    std::fclose(out);
  }

  inline void Init() {
    static bool done = false;
    if(done) {
      return;
    }
    done = true;
    const char* p = std::getenv("OBJECK_CFG_TRACE");
    if(p) {
      Reg().path = p;
      std::atexit([]() { Flush(); });
    }
  }

  inline MethodTrace* Find(StackMethod* m) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> guard(r.lock);
    auto it = r.methods.find(m);
    if(it != r.methods.end()) {
      return it->second;
    }
    MethodTrace* t = new MethodTrace;
    const std::wstring& w = m->GetName();
    t->name.assign(w.begin(), w.end());
    t->count = m->GetInstructionCount();
    t->executed.assign(t->count + 1, 0);
    t->sequential.assign(t->count + 1, 0);
    r.methods[m] = t;
    return t;
  }

  inline void AddEdge(MethodTrace* t, long from, long to) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> guard(r.lock);
    t->edges.insert((static_cast<unsigned long long>(from) << 32) | static_cast<unsigned long long>(to));
  }

  // called before the instruction at ip executes
  inline void Step(StackFrame* frame, long ip) {
    Init();
    if(Reg().path.empty() || !frame) {
      return;
    }
    ThreadState& ts = TS();
    if(frame != ts.frame || frame->method != ts.method) {
      ts.frame = frame;
      ts.method = frame->method;
      ts.trace = Find(frame->method);
      ts.prev = -1;
    }
    MethodTrace* t = ts.trace;
    if(ip < 0 || ip >= t->count) {
      return;
    }
    t->executed[ip] = 1;
    if(ts.prev >= 0) {
      if(ip == ts.prev + 1) {
        t->sequential[ts.prev] = 1;
      }
      else {
        AddEdge(t, ts.prev, ip);
      }
    }
    ts.prev = ip;
  }

  // a try region took a trap: control goes to the handler at ip, in this frame
  inline void Trap(StackFrame* frame, long ip) {
    Init();
    if(Reg().path.empty() || !frame) {
      return;
    }
    ThreadState& ts = TS();
    MethodTrace* t = Find(frame->method);
    long from = frame->cur_ip - 1;
    if(frame == ts.frame && frame->method == ts.method && ts.prev >= 0) {
      from = ts.prev;
    }
    if(from >= 0 && ip >= 0 && ip < t->count) {
      AddEdge(t, from, ip);
    }
    ts.frame = nullptr;
  }
}

#endif
