/***************************************************************************
 * Reference maps: which operand-stack words hold references, per instruction.
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifndef __REF_MAP_H__
#define __REF_MAP_H__

#include "../common.h"
#include <vector>
#include <string>
#include <utility>

// The collector does not know which operand-stack words are references: the
// interpreter pushes an object, an array and an Int the same way, so every word
// was scanned and relocated on a guess (docs/GC_ROOTS_DESIGN.md). A RefMap is
// what a method's bytecode says instead: for every instruction, the height of
// the operand stack on entry and the kind of each word on it, and the kind of
// the and/or slot, which no declaration types.
//
// One forward pass gives every pushed value an id and every instruction its
// entry stack as a list of ids. Values meet where control flow joins (the ids
// at the same depth are unified) and where they are used: a value stored to a
// typed local, passed as a typed parameter, returned, or used in arithmetic
// takes that kind, whatever produced it. That is how a trap's result, which
// carries no type of its own, and an element of an array whose element type
// the bytecode does not name get one. A value nothing types stays UNKNOWN, and
// one typed two ways (a reference returned as an Int, as GetInstanceID does)
// is CONFLICT; the collector treats both as references, as it treated every
// word before.
//
// The stack effects mirror dispatch.cpp's handlers. The map is checked against
// the live stack at every collection when OBJECK_GC_ROOTS=audit is set: the
// segments of a thread's frames must add up to its stack position.


  enum RefKind : uint8_t {
    RK_UNKNOWN = 0,
    RK_LIT,       // an integer literal: an Int, or Nil where a reference is expected
    RK_INT,
    RK_FLOAT,
    RK_REF,
    RK_CONFLICT
  };

  inline const wchar_t* RefKindName(uint8_t k) {
    switch(k) {
    case RK_LIT: return L"lit";
    case RK_INT: return L"int";
    case RK_FLOAT: return L"float";
    case RK_REF: return L"ref";
    case RK_CONFLICT: return L"conflict";
    default: return L"unknown";
    }
  }

  class RefMap {
  public:
    bool ok;
    // set with a failure that makes the method unsafe to run (an empty operand stack
    // popped, a join at two heights, a non-address read through), as opposed to one
    // that only means the map cannot be built (an instruction it does not model)
    bool refuse;
    std::wstring error;
    long error_ip;
    // per instruction: operand-stack height on entry, -1 where unreachable
    std::vector<int> height;
    // per instruction: words it pops before it can collect
    std::vector<int> pops;
    // per instruction: kinds of the entry stack, bottom first, at kinds[offset[i]]
    std::vector<uint32_t> offset;
    std::vector<uint8_t> kinds;
    // per instruction: kind of the and/or slot on entry (RK_UNKNOWN when unset)
    std::vector<uint8_t> andor;

    RefMap() : ok(false), refuse(false), error_ip(-1) {}

    inline uint8_t Kind(long ip, int depth) const {
      return kinds[offset[ip] + depth];
    }

    // 'verify' builds the map for Loader::Verify rather than for the JIT: a local the
    // method does not declare is an untyped slot rather than a failure (the frame bound
    // the loader checks is what keeps it safe), and a non-zero integer literal read
    // through as an array or object is a failure.
    static RefMap* Build(StackProgram* prgm, StackMethod* method, bool verify = false);
  };

  namespace ref_map_detail {

    // Words a method may push above its arguments: CallStack::OP_HEADROOM (1024)
    // less a margin for the words one instruction pushes past its entry height
    static const size_t MAX_OPERAND_GROWTH = 1000;

    inline uint8_t MergeKind(uint8_t a, uint8_t b) {
      if(a == b) return a;
      if(a == RK_UNKNOWN) return b;
      if(b == RK_UNKNOWN) return a;
      if(a == RK_CONFLICT || b == RK_CONFLICT) return RK_CONFLICT;
      // a literal is an Int or Nil: it takes either
      if(a == RK_LIT && (b == RK_INT || b == RK_REF)) return b;
      if(b == RK_LIT && (a == RK_INT || a == RK_REF)) return a;
      return RK_CONFLICT;
    }

    // a declaration's words, bottom (lowest address) first: a func-ref is its
    // packed class/method id, then its closure
    struct SlotType {
      uint8_t kind;
      uint8_t elem;
    };

    inline SlotType DclrType(ParamType type) {
      switch(type) {
      case CHAR_PARM:
      case INT_PARM:
        return { RK_INT, RK_UNKNOWN };
      case FLOAT_PARM:
        return { RK_FLOAT, RK_UNKNOWN };
      case BYTE_ARY_PARM:
      case CHAR_ARY_PARM:
      case INT_ARY_PARM:
        return { RK_REF, RK_INT };
      case FLOAT_ARY_PARM:
        return { RK_REF, RK_FLOAT };
      case OBJ_PARM:
        return { RK_REF, RK_UNKNOWN };
      case OBJ_ARY_PARM:
        return { RK_REF, RK_REF };
      default:
        return { RK_UNKNOWN, RK_UNKNOWN };
      }
    }

    // memory words of a declaration list (instance, class or closure memory),
    // laid out as the collector walks them (CheckMemory)
    inline void DclrLayout(StackDclr** dclrs, long num, std::vector<SlotType>& out) {
      out.clear();
      for(long i = 0; i < num; ++i) {
        if(dclrs[i]->type == FUNC_PARM) {
          out.push_back({ RK_INT, RK_UNKNOWN });
          out.push_back({ RK_REF, RK_UNKNOWN });
        }
        else {
          out.push_back(DclrType(dclrs[i]->type));
        }
      }
    }

    // A method's arguments as stack words, the first one deepest. GetParamCount
    // counts words, as the JIT's callbacks do: a func-ref parameter, one
    // declaration, is two (its closure, then its id on top, as LOAD_FUNC_VAR
    // pushes them)
    inline void ParamWords(StackMethod* method, std::vector<SlotType>& out) {
      out.clear();
      // a count below zero (a malformed image the loader should have refused) must not
      // become a 2^64-word table
      const size_t words = method->GetParamCount() > 0 ? (size_t)method->GetParamCount() : 0;
      StackDclr** dclrs = method->GetDeclarations();
      const long num_dclrs = method->GetNumberDeclarations();
      for(long i = 0; i < num_dclrs && out.size() < words; ++i) {
        if(dclrs[i]->type == FUNC_PARM) {
          out.push_back({ RK_REF, RK_UNKNOWN });
          out.push_back({ RK_INT, RK_UNKNOWN });
        }
        else {
          out.push_back(DclrType(dclrs[i]->type));
        }
      }
      // declarations that do not cover the words leave them untyped
      while(out.size() < words) {
        out.push_back({ RK_UNKNOWN, RK_UNKNOWN });
      }
      out.resize(words);
    }

    // an encoded return type ("i", "o.System.String", "b*", "m.(i,)~i", "n")
    // as stack words, bottom first
    inline void ReturnWords(const std::wstring& name, MemoryType type, std::vector<SlotType>& out) {
      out.clear();
      if(name.empty()) {
        switch(type) {
        case NIL_TYPE: return;
        case FLOAT_TYPE: out.push_back({ RK_FLOAT, RK_UNKNOWN }); return;
        case FUNC_TYPE: out.push_back({ RK_REF, RK_UNKNOWN }); out.push_back({ RK_INT, RK_UNKNOWN }); return;
        case BYTE_ARY_TYPE:
        case CHAR_ARY_TYPE: out.push_back({ RK_REF, RK_INT }); return;
        default: out.push_back({ RK_UNKNOWN, RK_UNKNOWN }); return;
        }
      }
      const wchar_t c = name[0];
      if(c == L'n') {
        return;
      }
      if(c == L'm') {
        out.push_back({ RK_REF, RK_UNKNOWN });
        out.push_back({ RK_INT, RK_UNKNOWN });
        return;
      }
      size_t stars = 0;
      for(size_t i = name.size(); i > 0 && name[i - 1] == L'*'; --i) {
        ++stars;
      }
      uint8_t base;
      switch(c) {
      case L'i': case L'l': case L'c': case L'b': base = RK_INT; break;
      case L'f': base = RK_FLOAT; break;
      case L'o': base = RK_REF; break;
      default: base = RK_UNKNOWN; break;
      }
      if(stars == 0) {
        out.push_back({ base, RK_UNKNOWN });
      }
      else if(stars == 1) {
        out.push_back({ RK_REF, base });
      }
      else {
        // Int[,] and Int[][] are spelled alike here: leave the elements to the uses
        out.push_back({ RK_REF, RK_UNKNOWN });
      }
    }

    class Builder {
      StackProgram* prgm;
      StackMethod* method;
      bool verify;
      StackInstr* instrs;
      long count;
      RefMap* map;

      // Values: union-find, each class with two kinds -- what produced it ('kind':
      // a typed load, NEW_*, LOAD_INST_MEM, a call's return type) and what it is
      // used as ('use': a store to a typed slot, an argument, a return,
      // arithmetic) -- an element kind (arrays) and an origin. The use decides:
      // a value the program goes on to treat as an Int is an Int, whatever
      // produced it. GetInstanceID() inlined at -opt s3 is LOAD_INST_MEM, a
      // reference, returned as an Int; typed by its producer it was relocated
      // when its object moved (#1452). An object still in use is kept by its
      // references, not by an Int that equals its address, and nothing turns
      // an Int back into one.
      enum Origin : uint8_t { ORIGIN_NONE = 0, ORIGIN_SELF, ORIGIN_CLS, ORIGIN_MIXED };
      std::vector<int> parent;
      std::vector<uint8_t> kind;
      std::vector<uint8_t> use;
      std::vector<uint8_t> elem;
      std::vector<uint8_t> origin;
      // Load-time verification (Loader::Verify), which does not change what the maps
      // say: 'deref' marks a value some instruction needs to be a reference (an array
      // or object it reads through, a call's receiver, a store to a reference local);
      // 'lit_ip' is where a non-zero integer literal entered the value, -1 for none.
      // Nil is the literal zero, so only a non-zero literal used as a reference is
      // refused: it is an address nobody allocated.
      std::vector<uint8_t> deref;
      std::vector<long> lit_ip;

      // entry state per instruction
      std::vector<std::vector<int> > state;
      std::vector<int> andor_state;
      // Load-time verification only: the and/or slot is reused for unrelated
      // temporaries (a select's selector, a lambda's closure), so its value meets at a
      // join even where nothing reads it any more. Meeting a literal with a closure
      // there put the literal's 'lit_ip' on a value read through as an object and
      // refused valid programs. The meets are held back until the slot's liveness is
      // known and made only where a path goes on to read it.
      struct AndOrJoin { long to; int known; int arriving; };
      std::vector<AndOrJoin> andor_joins;
      std::vector<std::pair<long, long> > edges;
      std::vector<bool> seen;
      std::vector<long> worklist;
      // the lowest the stack goes inside the instruction being stepped
      size_t min_height = 0;

      // locals by id
      std::vector<SlotType> locals;
      std::vector<bool> local_is_andor;

      // constraints resolved after the pass
      // 'load': the value comes from the element or field (it produces the value);
      // otherwise it goes there (a use of the value)
      struct ElemLink { int array; int value; bool load; };
      struct FieldLink { int receiver; long index; int value; bool load; };
      std::vector<ElemLink> elem_links;
      std::vector<FieldLink> field_links;
      std::vector<SlotType> self_fields;
      std::vector<SlotType> class_fields;

      int NewValue(uint8_t k, uint8_t e = RK_UNKNOWN, uint8_t o = ORIGIN_NONE) {
        const int id = static_cast<int>(parent.size());
        parent.push_back(id);
        kind.push_back(k);
        use.push_back(RK_UNKNOWN);
        elem.push_back(e);
        origin.push_back(o);
        deref.push_back(0);
        lit_ip.push_back(-1);
        return id;
      }

      int Find(int v) {
        while(parent[v] != v) {
          parent[v] = parent[parent[v]];
          v = parent[v];
        }
        return v;
      }

      void Union(int a, int b) {
        a = Find(a);
        b = Find(b);
        if(a == b) {
          return;
        }
        parent[b] = a;
        kind[a] = MergeKind(kind[a], kind[b]);
        use[a] = MergeKind(use[a], use[b]);
        elem[a] = MergeKind(elem[a], elem[b]);
        if(origin[a] != origin[b]) {
          origin[a] = ORIGIN_MIXED;
        }
        deref[a] |= deref[b];
        if(lit_ip[a] < 0) {
          lit_ip[a] = lit_ip[b];
        }
      }

      // the value is read through as an object or array
      void MarkRef(int v) {
        deref[Find(v)] = 1;
      }

      // a use of the value as k (and, an array, with elements e)
      void Constrain(int v, uint8_t k, uint8_t e = RK_UNKNOWN) {
        v = Find(v);
        if(k != RK_UNKNOWN) {
          use[v] = MergeKind(use[v], k);
        }
        if(e != RK_UNKNOWN) {
          elem[v] = MergeKind(elem[v], e);
        }
      }

      // what a value is: its use when it has one, else what produced it
      uint8_t Effective(int v) {
        v = Find(v);
        const uint8_t u = use[v];
        if(u == RK_INT || u == RK_FLOAT || u == RK_REF || u == RK_CONFLICT) {
          return u;
        }
        return kind[v] == RK_LIT ? RK_INT : kind[v];
      }

      // a producer's fact about the value
      void Produce(int v, uint8_t k, uint8_t e = RK_UNKNOWN) {
        v = Find(v);
        if(k != RK_UNKNOWN) {
          kind[v] = MergeKind(kind[v], k);
        }
        if(e != RK_UNKNOWN) {
          elem[v] = MergeKind(elem[v], e);
        }
      }

      bool Fail(long ip, const std::wstring& why, bool unsafe = false) {
        if(map->ok) {
          map->ok = false;
          map->refuse = unsafe;
          map->error = why;
          map->error_ip = ip;
        }
        return false;
      }

      // control reaches 'to' with 'stack' and 'andor'
      bool Flow(long from, long to, const std::vector<int>& stack, int andor) {
        if(to < 0 || to >= count) {
          return Fail(from, L"a jump outside the method", true);
        }
        if(verify) {
          edges.push_back({ from, to });
        }
        if(!seen[to]) {
          seen[to] = true;
          state[to] = stack;
          andor_state[to] = andor;
          worklist.push_back(to);
          return true;
        }
        std::vector<int>& known = state[to];
        if(known.size() != stack.size()) {
          return Fail(to, L"stack heights differ where control flow joins (" +
                      std::to_wstring(known.size()) + L" and " + std::to_wstring(stack.size()) + L")", true);
        }
        for(size_t i = 0; i < stack.size(); ++i) {
          Union(known[i], stack[i]);
        }
        // every path carries the slot's value (Run starts it as the zero the frame
        // is entered with), so it meets like a stack word
        if(andor >= 0 && andor_state[to] >= 0) {
          if(verify) {
            andor_joins.push_back({ to, andor_state[to], andor });
          }
          else {
            Union(andor_state[to], andor);
          }
        }
        return true;
      }

      bool Pop(std::vector<int>& stack, long ip, int& v) {
        if(stack.empty()) {
          return Fail(ip, L"the operand stack runs out", true);
        }
        v = stack.back();
        stack.pop_back();
        if(stack.size() < min_height) {
          min_height = stack.size();
        }
        return true;
      }

      bool PopKind(std::vector<int>& stack, long ip, uint8_t k, uint8_t e = RK_UNKNOWN) {
        int v;
        if(!Pop(stack, ip, v)) {
          return false;
        }
        Constrain(v, k, e);
        return true;
      }

      // a value read through as an array or an object: the same pop, and a non-zero
      // integer literal reaching it is refused. A reference-typed local, a return or a
      // parameter may hold an Int the program calls an object (an enum is one), so only
      // a use that reads memory through it counts.
      bool PopRef(std::vector<int>& stack, long ip, uint8_t e = RK_UNKNOWN) {
        int v;
        if(!Pop(stack, ip, v)) {
          return false;
        }
        Constrain(v, RK_REF, e);
        MarkRef(v);
        return true;
      }

      bool PopN(std::vector<int>& stack, long ip, long n, uint8_t k = RK_UNKNOWN) {
        for(long i = 0; i < n; ++i) {
          if(!PopKind(stack, ip, k)) {
            return false;
          }
        }
        return true;
      }

      void PushWords(std::vector<int>& stack, const std::vector<SlotType>& words) {
        for(size_t i = 0; i < words.size(); ++i) {
          stack.push_back(NewValue(words[i].kind, words[i].elem));
        }
      }

      bool AndOrLocal(long id) {
        return id >= 0 && id < (long)local_is_andor.size() && local_is_andor[id];
      }

      bool LocalType(long ip, long id, SlotType& out) {
        if(id < 0 || id >= (long)locals.size()) {
          if(verify && id >= 0) {
            out = { RK_UNKNOWN, RK_UNKNOWN };
            return true;
          }
          return Fail(ip, L"a local beyond the method's declarations (id " + std::to_wstring(id) + L")");
        }
        out = locals[id];
        return true;
      }

      bool Step(long ip);
      void ResolveLinks();

      // the and/or slot's meets at the joins where it is read before it is written again
      void MeetLiveAndOrJoins() {
        if(andor_joins.empty()) {
          return;
        }
        // backward liveness of the slot over the edges Flow saw
        std::vector<char> live(count, 0);
        bool changed = true;
        while(changed) {
          changed = false;
          for(size_t i = edges.size(); i-- > 0; ) {
            const long from = edges[i].first;
            const long to = edges[i].second;
            const StackInstr& in = instrs[from];
            bool now;
            if(in.GetType() == LOAD_LOCL_INT_VAR && AndOrLocal(in.GetOperand())) {
              now = true;
            }
            else if((in.GetType() == STOR_LOCL_INT_VAR || in.GetType() == COPY_LOCL_INT_VAR) && AndOrLocal(in.GetOperand())) {
              now = false;
            }
            else {
              now = live[to] != 0;
              // other edges out of this instruction (a jump has two) also count
              if(!now) {
                continue;
              }
            }
            if(now && !live[from]) {
              live[from] = 1;
              changed = true;
            }
          }
        }
        for(size_t i = 0; i < andor_joins.size(); ++i) {
          if(live[andor_joins[i].to]) {
            Union(andor_joins[i].known, andor_joins[i].arriving);
          }
        }
      }

    public:
      Builder(StackProgram* p, StackMethod* m, RefMap* r, bool v = false) : prgm(p), method(m), map(r), verify(v) {
        instrs = m->GetInstructions();
        count = m->GetInstructionCount();
      }

      void Run();
    };

    inline bool Builder::Step(long ip) {
      std::vector<int> stack = state[ip];
      int andor = andor_state[ip];
      StackInstr* instr = &instrs[ip];
      const long op1 = instr->GetOperand();
      const long op2 = instr->GetOperand2();
      const int entry_height = static_cast<int>(stack.size());
      bool falls_through = true;
      SlotType slot;
      int v;
      min_height = stack.size();

      switch(instr->GetType()) {
      case LOAD_INT_LIT: {
        const int lit = NewValue(RK_LIT);
        if(instr->GetInt64Operand() != 0) {
          lit_ip[lit] = ip;
        }
        stack.push_back(lit);
      }
        break;

      case LOAD_CHAR_LIT:
        stack.push_back(NewValue(RK_INT));
        break;

      case LOAD_FLOAT_LIT:
      case NAN_INT: case INF_INT: case NEG_INF_INT:
      case NAN_FLOAT: case INF_FLOAT: case NEG_INF_FLOAT:
      case RAND_FLOAT:
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case LOAD_LOCL_INT_VAR:
        if(!LocalType(ip, op1, slot)) return false;
        if(AndOrLocal(op1)) {
          stack.push_back(andor);
        }
        else {
          stack.push_back(NewValue(slot.kind, slot.elem));
        }
        break;

      case STOR_LOCL_INT_VAR:
      case COPY_LOCL_INT_VAR:
        if(!LocalType(ip, op1, slot)) return false;
        if(instr->GetType() == STOR_LOCL_INT_VAR) {
          if(!Pop(stack, ip, v)) return false;
        }
        else {
          if(stack.empty()) return Fail(ip, L"the operand stack runs out", true);
          v = stack.back();
        }
        if(AndOrLocal(op1)) {
          andor = v;
        }
        else {
          Constrain(v, slot.kind, slot.elem);
        }
        break;

      case LOAD_CLS_INST_INT_VAR: {
        int receiver;
        if(!Pop(stack, ip, receiver)) return false;
        MarkRef(receiver);
        const int value = NewValue(RK_UNKNOWN);
        field_links.push_back({ receiver, op1, value, true });
        stack.push_back(value);
      }
        break;

      case STOR_CLS_INST_INT_VAR: {
        int receiver;
        if(!Pop(stack, ip, receiver) || !Pop(stack, ip, v)) return false;
        MarkRef(receiver);
        field_links.push_back({ receiver, op1, v, false });
      }
        break;

      case COPY_CLS_INST_INT_VAR: {
        int receiver;
        if(!Pop(stack, ip, receiver)) return false;
        MarkRef(receiver);
        if(stack.empty()) return Fail(ip, L"the operand stack runs out", true);
        field_links.push_back({ receiver, op1, stack.back(), false });
      }
        break;

      case LOAD_FLOAT_VAR:
        if(op2 != LOCL && !PopKind(stack, ip, RK_UNKNOWN)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case STOR_FLOAT_VAR:
        if(op2 != LOCL && !PopKind(stack, ip, RK_UNKNOWN)) return false;
        if(!PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case COPY_FLOAT_VAR:
        if(op2 != LOCL && !PopKind(stack, ip, RK_UNKNOWN)) return false;
        if(stack.empty()) return Fail(ip, L"the operand stack runs out", true);
        Constrain(stack.back(), RK_FLOAT);
        break;

      case LOAD_FUNC_VAR:
        if(op2 != LOCL && !PopKind(stack, ip, RK_UNKNOWN)) return false;
        stack.push_back(NewValue(RK_REF));
        stack.push_back(NewValue(RK_INT));
        break;

      case STOR_FUNC_VAR:
        if(op2 != LOCL && !PopKind(stack, ip, RK_UNKNOWN)) return false;
        if(!PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_REF)) return false;
        break;

      case LOAD_CLS_MEM:
        // class memory is not a heap object: never marked, never moved
        stack.push_back(NewValue(RK_INT, RK_UNKNOWN, ORIGIN_CLS));
        break;

      case LOAD_INST_MEM:
        stack.push_back(NewValue(RK_REF, RK_UNKNOWN, ORIGIN_SELF));
        break;

      case LOAD_BYTE_ARY_ELM:
      case LOAD_CHAR_ARY_ELM:
        if(!PopRef(stack, ip) || !PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case LOAD_FLOAT_ARY_ELM:
        if(!PopRef(stack, ip, RK_FLOAT) || !PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case LOAD_INT_ARY_ELM: {
        int array;
        if(!Pop(stack, ip, array)) return false;
        Constrain(array, RK_REF);
        MarkRef(array);
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        const int value = NewValue(RK_UNKNOWN);
        elem_links.push_back({ array, value, true });
        stack.push_back(value);
      }
        break;

      case STOR_BYTE_ARY_ELM:
      case STOR_CHAR_ARY_ELM:
        if(!PopRef(stack, ip) || !PopN(stack, ip, op1, RK_INT) || !PopKind(stack, ip, RK_INT)) return false;
        break;

      case STOR_FLOAT_ARY_ELM:
        if(!PopRef(stack, ip, RK_FLOAT) || !PopN(stack, ip, op1, RK_INT) || !PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case STOR_INT_ARY_ELM: {
        int array;
        if(!Pop(stack, ip, array)) return false;
        Constrain(array, RK_REF);
        MarkRef(array);
        if(!PopN(stack, ip, op1, RK_INT) || !Pop(stack, ip, v)) return false;
        elem_links.push_back({ array, v, false });
      }
        break;

      case LOAD_ARY_SIZE:
        if(!PopRef(stack, ip)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      // comparisons take references too (identity, Nil checks)
      case EQL_INT: case NEQL_INT: case LES_INT: case GTR_INT: case LES_EQL_INT: case GTR_EQL_INT:
        if(!PopN(stack, ip, 2)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case EQL_FLOAT: case NEQL_FLOAT: case LES_FLOAT: case GTR_FLOAT: case LES_EQL_FLOAT: case GTR_EQL_FLOAT:
        if(!PopN(stack, ip, 2, RK_FLOAT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case AND_INT: case OR_INT: case ADD_INT: case SUB_INT: case MUL_INT: case DIV_INT: case MOD_INT:
      case BIT_AND_INT: case BIT_OR_INT: case BIT_XOR_INT: case SHL_INT: case SHR_INT:
        if(!PopN(stack, ip, 2, RK_INT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case BIT_NOT_INT:
        if(!PopKind(stack, ip, RK_INT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case ADD_FLOAT: case SUB_FLOAT: case MUL_FLOAT: case DIV_FLOAT:
      case ATAN2_FLOAT: case MOD_FLOAT: case POW_FLOAT:
        if(!PopN(stack, ip, 2, RK_FLOAT)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case FLOR_FLOAT: case CEIL_FLOAT: case TRUNC_FLOAT: case SIN_FLOAT: case COS_FLOAT: case TAN_FLOAT:
      case ASIN_FLOAT: case ACOS_FLOAT: case ATAN_FLOAT: case LOG2_FLOAT: case CBRT_FLOAT: case COSH_FLOAT:
      case SINH_FLOAT: case TANH_FLOAT: case ACOSH_FLOAT: case ASINH_FLOAT: case ATANH_FLOAT: case LOG_FLOAT:
      case ROUND_FLOAT: case EXP_FLOAT: case LOG10_FLOAT: case SQRT_FLOAT: case GAMMA_FLOAT:
        if(!PopKind(stack, ip, RK_FLOAT)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case I2F:
        if(!PopKind(stack, ip, RK_INT)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case F2I:
        if(!PopKind(stack, ip, RK_FLOAT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case S2I:
        if(!PopKind(stack, ip, RK_REF) || !PopKind(stack, ip, RK_INT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case S2F:
        if(!PopKind(stack, ip, RK_REF)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case I2S:
        if(!PopKind(stack, ip, RK_REF) || !PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_INT)) return false;
        break;

      case F2S:
        if(!PopKind(stack, ip, RK_REF) || !PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case MTHD_CALL:
      case MTHD_CALL_JIT: {
        StackClass* cls = prgm->GetClass(op1);
        StackMethod* callee = cls ? cls->GetMethod(op2) : nullptr;
        if(!callee) {
          return Fail(ip, L"a call to an unknown method");
        }
        // the receiver on top, then the arguments, the last one nearest it
        int receiver_value;
        if(!Pop(stack, ip, receiver_value)) return false;
        MarkRef(receiver_value);
        std::vector<SlotType> params;
        ParamWords(callee, params);
        for(size_t i = params.size(); i > 0; --i) {
          if(!PopKind(stack, ip, params[i - 1].kind, params[i - 1].elem)) return false;
        }
        std::vector<SlotType> rtrn;
        ReturnWords(callee->GetReturnName(), callee->GetReturn(), rtrn);
        PushWords(stack, rtrn);
      }
        break;

      case DYN_MTHD_CALL:
      case DYN_MTHD_CALL_JIT: {
        // the func-ref's id on top, its closure below, then operand arguments
        if(!PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_REF) || !PopN(stack, ip, op1)) return false;
        std::vector<SlotType> rtrn;
        ReturnWords(L"", (MemoryType)op2, rtrn);
        PushWords(stack, rtrn);
      }
        break;

      case JMP:
        if(op2 >= 0 && !PopKind(stack, ip, RK_INT)) return false;
        if(!Flow(ip, op1, stack, andor)) return false;
        falls_through = op2 >= 0;
        break;

      case LBL:
      case TRY_END:
      case END_STMTS:
      case ASYNC_MTHD_CALL:
      case THREAD_JOIN:
      case THREAD_MUTEX:
      case EXT_LIB_LOAD:
      case EXT_LIB_UNLOAD:
      case EXT_LIB_FUNC_CALL:
        break;

      case TRY_START:
        // a handler resumes with the stack as it was here (PushTryHandler)
        if(!Flow(ip, op1, stack, andor)) return false;
        break;

      case RTRN: {
        std::vector<SlotType> rtrn;
        ReturnWords(method->GetReturnName(), method->GetReturn(), rtrn);
        if(stack.size() >= rtrn.size()) {
          for(size_t i = 0; i < rtrn.size(); ++i) {
            Constrain(stack[stack.size() - rtrn.size() + i], rtrn[i].kind, rtrn[i].elem);
          }
        }
        falls_through = false;
      }
        break;

      case NEW_BYTE_ARY:
      case NEW_CHAR_ARY:
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF, RK_INT));
        break;

      case NEW_INT_ARY:
        // an Int[] (GC roots phase 6): its elements are Ints
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF, RK_INT));
        break;

      case NEW_OBJ_ARY:
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF));
        break;

      case NEW_FLOAT_ARY:
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF, RK_FLOAT));
        break;

      case NEW_OBJ_INST:
      case NEW_FUNC_INST:
        stack.push_back(NewValue(RK_REF));
        break;

      case CPY_BYTE_ARY: case CPY_CHAR_ARY: case CPY_INT_ARY: case CPY_FLOAT_ARY:
        // length, source offset, source, destination offset, destination
        if(!PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_REF) ||
           !PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_REF)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case ZERO_BYTE_ARY: case ZERO_CHAR_ARY: case ZERO_INT_ARY: case ZERO_FLOAT_ARY:
        if(!PopKind(stack, ip, RK_REF)) return false;
        break;

      case OBJ_INST_CAST: {
        // the same reference comes back (or Nil)
        if(!Pop(stack, ip, v)) return false;
        Constrain(v, RK_REF);
        stack.push_back(v);
      }
        break;

      case OBJ_TYPE_OF:
        if(!PopKind(stack, ip, RK_REF)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case TRAP: {
        // the trap id and its arguments, none typed here
        if(!PopN(stack, ip, op1)) return false;
        // A TRAP pushes nothing, as the JIT models it, except where its result is
        // the method's: a trap right before the RTRN of a method that returns a
        // value (Deserializer->ReadInt is LOAD_INT_LIT id; TRAP args=1; RTRN, and
        // the trap pushes the Int). Such a method never compiles: the JIT finds
        // nothing to return. The census of the suite's traps (#1457) found every
        // value-pushing TRAP in this position and none elsewhere.
        if(ip + 1 < count && instrs[ip + 1].GetType() == RTRN) {
          std::vector<SlotType> rtrn;
          ReturnWords(method->GetReturnName(), method->GetReturn(), rtrn);
          while(stack.size() < rtrn.size()) {
            stack.push_back(NewValue(RK_UNKNOWN));
          }
        }
      }
        break;

      case TRAP_RTRN:
        // one word back, typed by its uses (the JIT models it the same way)
        if(!PopN(stack, ip, op1)) return false;
        stack.push_back(NewValue(RK_UNKNOWN));
        break;

      case SWAP_INT: {
        if(stack.size() < 2) return Fail(ip, L"the operand stack runs out", true);
        std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
      }
        break;

      case POP_INT:
        if(!Pop(stack, ip, v)) return false;
        break;

      case POP_FLOAT:
        if(!PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case THREAD_SLEEP:
        if(!PopKind(stack, ip, RK_INT)) return false;
        break;

      case CRITICAL_START:
      case CRITICAL_END:
        if(!PopKind(stack, ip, RK_REF)) return false;
        break;

      case JMP_TABLE: {
        if(!PopKind(stack, ip, RK_INT)) return false;
        const long range = op2;
        if(range < 0 || ip + range >= count) {
          return Fail(ip, L"a select table that runs past the method");
        }
        for(long i = 1; i <= range; ++i) {
          if(!Flow(ip, instrs[ip + i].GetOperand(), stack, andor)) return false;
        }
        if(!Flow(ip, instr->GetOperand3(), stack, andor)) return false;
        falls_through = false;
      }
        break;

      default:
        return Fail(ip, L"an instruction the map does not model (type " + std::to_wstring(instr->GetType()) + L")");
      }

      // An instruction that collects does so with its inputs popped (an array's
      // dimensions, a call's receiver and arguments) or not yet popped (a new
      // object): the collector accepts any height from this one to the entry's
      map->pops[ip] = entry_height - static_cast<int>(min_height);

      if(falls_through) {
        return Flow(ip, ip + 1, stack, andor);
      }
      return true;
    }

    inline void Builder::ResolveLinks() {
      // field kinds come from the receiver's declarations, when the receiver is
      // known to be this frame's self or class memory
      for(size_t pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for(size_t i = 0; i < field_links.size(); ++i) {
          const FieldLink& link = field_links[i];
          const int r = Find(link.receiver);
          const std::vector<SlotType>* fields = nullptr;
          if(origin[r] == ORIGIN_SELF && !method->IsLambda()) {
            fields = &self_fields;
          }
          else if(origin[r] == ORIGIN_CLS) {
            fields = &class_fields;
          }
          if(fields && link.index >= 0 && link.index < (long)fields->size()) {
            const int value = Find(link.value);
            const uint8_t before_kind = kind[value];
            const uint8_t before_use = use[value];
            const uint8_t before_elem = elem[value];
            if(link.load) {
              Produce(value, (*fields)[link.index].kind, (*fields)[link.index].elem);
            }
            else {
              Constrain(value, (*fields)[link.index].kind, (*fields)[link.index].elem);
            }
            const int now = Find(value);
            changed |= kind[now] != before_kind || use[now] != before_use || elem[now] != before_elem;
          }
        }
        for(size_t i = 0; i < elem_links.size(); ++i) {
          const int array = Find(elem_links[i].array);
          const int value = Find(elem_links[i].value);
          const uint8_t ek = elem[array];
          const uint8_t vk = Effective(value);
          if((ek == RK_INT || ek == RK_FLOAT || ek == RK_REF) && vk != ek) {
            const uint8_t before = elem_links[i].load ? kind[value] : use[value];
            if(elem_links[i].load) {
              Produce(value, ek);
            }
            else {
              Constrain(value, ek);
            }
            changed |= (elem_links[i].load ? kind[Find(value)] : use[Find(value)]) != before;
          }
          else if(ek == RK_UNKNOWN && (vk == RK_INT || vk == RK_FLOAT || vk == RK_REF)) {
            elem[array] = vk;
            changed = true;
          }
        }
        if(!changed) {
          break;
        }
      }
    }

    inline void Builder::Run() {
      map->ok = true;
      map->height.assign(count, -1);
      map->pops.assign(count, -1);
      map->offset.assign(count, 0);
      map->andor.assign(count, RK_UNKNOWN);
      state.assign(count, std::vector<int>());
      andor_state.assign(count, -1);
      seen.assign(count, false);

      // locals by id: the and/or slot first when the method has one
      StackDclr** dclrs = method->GetDeclarations();
      const long num_dclrs = method->GetNumberDeclarations();
      if(method->HasAndOr()) {
        locals.push_back({ RK_UNKNOWN, RK_UNKNOWN });
        local_is_andor.push_back(true);
      }
      for(long i = 0; i < num_dclrs; ++i) {
        if(dclrs[i]->type == FUNC_PARM) {
          locals.push_back({ RK_INT, RK_UNKNOWN });
          locals.push_back({ RK_REF, RK_UNKNOWN });
          local_is_andor.push_back(false);
          local_is_andor.push_back(false);
        }
        else {
          locals.push_back(DclrType(dclrs[i]->type));
          local_is_andor.push_back(false);
        }
      }

      StackClass* cls = method->GetClass();
      if(cls) {
        DclrLayout(cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), self_fields);
        DclrLayout(cls->GetClassDeclarations(), cls->GetNumberClassDeclarations(), class_fields);
      }

      if(count < 1) {
        Fail(0, L"a method with no instructions");
        return;
      }

      // An interpreted call leaves the arguments on the operand stack, and the
      // method's first instructions store them (the receiver went to mem[0])
      std::vector<int> entry;
      std::vector<SlotType> params;
      ParamWords(method, params);
      for(size_t i = 0; i < params.size(); ++i) {
        entry.push_back(NewValue(RK_UNKNOWN));
      }
      // The and/or slot holds what the frame was entered with, zero, until a path
      // stores to it. Starting it as that value keeps one value on every path, so
      // what two paths stored meets where they join. Without it, a join first
      // reached by a path that had stored nothing had no value there, and what a
      // later path stored never met it: Query.Structured.SqlRun:Plain's slot was
      // typed Int where it held an object.
      Flow(0, 0, entry, method->HasAndOr() ? NewValue(RK_LIT) : -1);

      while(!worklist.empty() && map->ok) {
        const long ip = worklist.back();
        worklist.pop_back();
        Step(ip);
      }
      if(!map->ok) {
        return;
      }

      if(verify) {
        MeetLiveAndOrJoins();
      }

      ResolveLinks();

      if(verify) {
        for(size_t v = 0; v < parent.size(); ++v) {
          if(Find(static_cast<int>(v)) == static_cast<int>(v) && deref[v] && lit_ip[v] >= 0) {
            Fail(lit_ip[v], L"an integer literal used as a reference", true);
            return;
          }
        }
      }

      // The operand stack has OP_HEADROOM (1024) words of room above a call's
      // arguments, checked once at entry (call_stack.h), and nothing inside the
      // method checks again. Joins at one height keep a loop from growing the stack,
      // so the deepest entry height bounds the method; a body that pushes past the
      // room writes beyond the reserved stack. The compiler cannot get near it (it
      // refuses nesting far below the limit), so this only refuses a crafted image.
      if(verify) {
        size_t deepest = 0;
        for(long ip = 0; ip < count; ++ip) {
          if(seen[ip] && state[ip].size() > deepest) {
            deepest = state[ip].size();
          }
        }
        if(deepest > entry.size() + MAX_OPERAND_GROWTH) {
          Fail(0, L"the operand stack grows to " + std::to_wstring(deepest - entry.size()) +
                  L" words past the arguments, more than a call's room", true);
          return;
        }
      }

      for(long ip = 0; ip < count; ++ip) {
        if(!seen[ip]) {
          continue;
        }
        const std::vector<int>& stack = state[ip];
        map->height[ip] = static_cast<int>(stack.size());
        map->offset[ip] = static_cast<uint32_t>(map->kinds.size());
        for(size_t i = 0; i < stack.size(); ++i) {
          map->kinds.push_back(Effective(stack[i]));
        }
        if(andor_state[ip] >= 0) {
          map->andor[ip] = Effective(andor_state[ip]);
        }
      }
    }
  }

  inline RefMap* RefMap::Build(StackProgram* prgm, StackMethod* method, bool verify) {
    RefMap* map = new RefMap;
    ref_map_detail::Builder builder(prgm, method, map, verify);
    builder.Run();
    return map;
  }

#endif
