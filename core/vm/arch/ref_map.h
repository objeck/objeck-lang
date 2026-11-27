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
#include "../../shared/trap_signatures.h"
#include <vector>
#include <string>
#include <cwchar>
#include <utility>
#include <algorithm>
#include <unordered_map>

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

  // What Loader::Verify keeps across the methods of one program: for each method,
  // which of its parameter words and whether its return are typed with a class of
  // the program or an array -- a word that is then a reference or Nil, never a
  // number. A method's name spells its parameter types, and an enum's name is not
  // a class's: `Paint(c : Color)` takes an Int, `Print(s : String)` an object.
  namespace ref_map_detail {
    class Builder;
  }
  class RefMap;

  struct VerifyContext {
    VerifyContext() : builder(nullptr), map(nullptr) {}
    ~VerifyContext();
    // One builder and one map serve every method of a program (RefMap::Verify):
    // their tables keep their storage from one method to the next, where a builder
    // per method allocated and freed some forty vectors each time.
    ref_map_detail::Builder* builder;
    RefMap* map;

    struct Signature {
      std::vector<uint8_t> object_param;   // by parameter word, the first parameter first
      bool object_return = false;
      // the class a parameter's or the return's name spells, -1 where it spells none
      // or one that cannot be told from a generic's parameter (an unqualified name)
      std::vector<long> param_class;
      long return_class = -1;
      // "m.(i,)~i" for both words of a function-typed parameter, empty otherwise
      std::vector<std::wstring> param_function;
    };
    std::unordered_map<StackMethod*, Signature> signatures;

    // Whether a method uses the instance it is called with. A function (the
    // language's static method) is called with whatever instance its caller has,
    // so a receiver of another class is only wrong where the callee reads it: it
    // loads a field through it, stores, passes or returns it, hands it to a trap
    // or to a method that does one of those ('hands_self_to', met once every
    // method has been walked).
    struct MethodFacts {
      bool reads_self = false;
      std::vector<StackMethod*> hands_self_to;
    };
    std::unordered_map<StackMethod*, MethodFacts> methods;
    // a call whose receiver is known to be an instance of 'cls', which is not the
    // callee's class nor one that inherits from it
    struct ReceiverCheck { StackMethod* caller; long ip; StackMethod* callee; long cls; };
    std::vector<ReceiverCheck> receiver_checks;
    // by class id: 1 for a class some class lists as an interface (filled on first use)
    std::vector<char> is_interface;
    // by class id: the largest instance among the class and those that inherit
    // from it, in bytes (filled on first use)
    std::vector<INT64_VALUE> family_size;
  };

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
    static RefMap* Build(StackProgram* prgm, StackMethod* method, bool verify = false, VerifyContext* context = nullptr);

    // Load-time verification of one method with the context's builder: true when
    // the method must be refused, with the rule it broke and where. The map
    // itself is not kept.
    static bool Verify(StackProgram* prgm, StackMethod* method, VerifyContext* context, std::wstring& why, long& where);
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
      // load-time verification only: what a reference is (SHAPE_NONE where the
      // declaration does not say)
      uint8_t shape;
      // 1 for a function value's first word (its id), 2 for its second (its closure)
      uint8_t part;
      // which array a declaration names (ArrayKind bits), 0 where it names none
      uint8_t ary;
    };

    // Load-time verification: an array's elements are one, four (or two) or eight
    // bytes wide and Ints, Floats or references, and the interpreter reads them as
    // whatever the instruction says. A Byte[] indexed as an Int[] is read eight
    // times past its end. An Int[] and an object array are not told apart: both
    // are read with the same instructions, and an array of an enum is declared
    // one and allocated as the other ('ColumnType->New[0]' is NEW_OBJ_ARY, and
    // the parameter that takes it an INT_ARY_PARM).
    enum ArrayKind : uint8_t { AK_BYTE = 1, AK_CHAR = 2, AK_WORD = 4, AK_FLOAT = 8, AK_ANY = 15 };

    // An object and an array are both references, and the interpreter reads them
    // differently: an array's size and dimensions sit in its first words, where an
    // object keeps its fields.
    enum Shape : uint8_t { SHAPE_NONE = 0, SHAPE_OBJECT = 1, SHAPE_ARRAY = 2 };

    inline SlotType DclrType(ParamType type) {
      switch(type) {
      case CHAR_PARM:
      case INT_PARM:
        return { RK_INT, RK_UNKNOWN };
      case FLOAT_PARM:
        return { RK_FLOAT, RK_UNKNOWN };
      case BYTE_ARY_PARM:
        return { RK_REF, RK_INT, SHAPE_ARRAY, 0, AK_BYTE };
      case CHAR_ARY_PARM:
        return { RK_REF, RK_INT, SHAPE_ARRAY, 0, AK_CHAR };
      case INT_ARY_PARM:
        return { RK_REF, RK_INT, SHAPE_ARRAY, 0, AK_WORD };
      case FLOAT_ARY_PARM:
        return { RK_REF, RK_FLOAT, SHAPE_ARRAY, 0, AK_FLOAT };
      case OBJ_PARM:
        return { RK_REF, RK_UNKNOWN, SHAPE_OBJECT };
      case OBJ_ARY_PARM:
        return { RK_REF, RK_REF, SHAPE_ARRAY, 0, AK_WORD };
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
          out.push_back({ RK_INT, RK_UNKNOWN, SHAPE_NONE, 1 });
          out.push_back({ RK_REF, RK_UNKNOWN, SHAPE_NONE, 2 });
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
          out.push_back({ RK_REF, RK_UNKNOWN, SHAPE_NONE, 2 });
          out.push_back({ RK_INT, RK_UNKNOWN, SHAPE_NONE, 1 });
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
        case FUNC_TYPE: out.push_back({ RK_REF, RK_UNKNOWN, SHAPE_NONE, 2 }); out.push_back({ RK_INT, RK_UNKNOWN, SHAPE_NONE, 1 }); return;
        case BYTE_ARY_TYPE:
        case CHAR_ARY_TYPE: out.push_back({ RK_REF, RK_INT, SHAPE_ARRAY }); return;
        default: out.push_back({ RK_UNKNOWN, RK_UNKNOWN }); return;
        }
      }
      const wchar_t c = name[0];
      if(c == L'n') {
        return;
      }
      if(c == L'm') {
        out.push_back({ RK_REF, RK_UNKNOWN, SHAPE_NONE, 2 });
        out.push_back({ RK_INT, RK_UNKNOWN, SHAPE_NONE, 1 });
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
        out.push_back({ base, RK_UNKNOWN, static_cast<uint8_t>(base == RK_REF ? SHAPE_OBJECT : SHAPE_NONE) });
      }
      else if(stars == 1) {
        uint8_t ary = 0;
        switch(c) {
        case L'b': case L'l': ary = AK_BYTE; break;
        case L'c': ary = AK_CHAR; break;
        case L'i': case L'o': ary = AK_WORD; break;
        case L'f': ary = AK_FLOAT; break;
        default: break;
        }
        out.push_back({ RK_REF, base, SHAPE_ARRAY, 0, ary });
      }
      else {
        // Int[,] and Int[][] are spelled alike here: leave the elements to the uses
        out.push_back({ RK_REF, RK_UNKNOWN, SHAPE_ARRAY });
      }
    }

    // 'cls' is 'ancestor', inherits from it, or implements it (an interface's own
    // parents count). Every class is a System.Base.
    inline bool ClassIsA(StackProgram* prgm, long cls, long ancestor) {
      if(cls == ancestor) {
        return true;
      }
      const long num = prgm->GetClassNumber();
      if(cls < 0 || cls >= num || ancestor < 0 || ancestor >= num) {
        return false;
      }
      long* hierarchy = prgm->GetHierarchy();
      long** interfaces = prgm->GetInterfaces();
      // the usual case: up the parents, and each one's interfaces
      long at = cls;
      for(long depth = 0; at >= 0 && at < num && depth <= num; ++depth) {
        if(at == ancestor) {
          return true;
        }
        if(interfaces && interfaces[at]) {
          for(long* inf = interfaces[at]; *inf != INF_ENDING; ++inf) {
            if(*inf == ancestor) {
              return true;
            }
          }
        }
        at = hierarchy ? hierarchy[at] : -1;
      }
      StackClass* target = prgm->GetClass(ancestor);
      if(target && target->GetName() == L"System.Base") {
        return true;
      }
      // an interface that extends another
      std::vector<char> seen(num, 0);
      std::vector<long> work(1, cls);
      seen[cls] = 1;
      while(!work.empty()) {
        at = work.back();
        work.pop_back();
        if(at == ancestor) {
          return true;
        }
        const long parent = hierarchy ? hierarchy[at] : -1;
        if(parent >= 0 && parent < num && !seen[parent]) {
          seen[parent] = 1;
          work.push_back(parent);
        }
        if(interfaces && interfaces[at]) {
          for(long* inf = interfaces[at]; *inf != INF_ENDING; ++inf) {
            if(*inf >= 0 && *inf < num && !seen[*inf]) {
              seen[*inf] = 1;
              work.push_back(*inf);
            }
          }
        }
      }
      return false;
    }

    // Two families of rules are written, pass the valid-program corpus with no
    // refusal, and are switched off: on the 30,000-change campaign neither refused
    // a program that went on to crash (docs/design/OBE_TYPE_VERIFIER.md has the
    // numbers), and a rule that buys nothing is only a chance to refuse a valid
    // program. CLASS_RULES: a reference's class where an image spells one, the
    // receiver, argument, return and trap-object checks on it, and the kind of an
    // instance variable. ARRAY_KIND_RULES: Byte, Char, Float and word arrays told
    // apart. They become worth their cost when declarations carry a class.
    static const bool CLASS_RULES = false;
    static const bool ARRAY_KIND_RULES = false;

    class Builder {
      StackProgram* prgm;
      StackMethod* method;
      bool verify;
      VerifyContext* context;
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
      // Load-time verification only, one per value (empty for the JIT's maps): what
      // made the value and what it is used as, kept apart from 'kind' and 'use',
      // which the maps are built from and which fold a disagreement into
      // RK_CONFLICT. A value made as one thing and read as another is what the
      // interpreter cannot survive: it has no types at run time, so a number read
      // through as an address, or an object read as an array, faults in the VM.
      enum Fact : uint8_t {
        F_LIT = 1,      // an integer literal
        F_INT = 2,      // arithmetic, a comparison, an Int local, parameter, field or return
        F_FLOAT = 4,
        F_REF = 8,
        F_CLS = 16,     // class memory (LOAD_CLS_MEM): not a heap object
        F_FUNC = 32     // a function value's id word, loaded, passed in or returned
      };
      struct Facts {
        uint8_t made = 0;
        uint8_t used = 0;
        uint8_t shape_made = 0;
        uint8_t shape_used = 0;
        long deref_ip = -1;
        long object_ip = -1;
        long array_ip = -1;
        long float_ip = -1;
        long word_ip = -1;
        // the class NEW_OBJ_INST made it an instance of: NO_CLASS for none, MANY_CLASSES
        // where two meet
        long cls_made = NO_CLASS;
        // the method's own instance (LOAD_INST_MEM), and whether anything took the
        // value off the stack other than a call to a method that is not virtual
        bool from_self = false;
        bool consumed = false;
        // where the value was first used as a function value's id word, -1 for never
        long func_ip = -1;
        // the arrays it was made as (ArrayKind bits), those every use of it allows,
        // and the first use that allows fewer than all
        uint8_t ary_made = 0;
        uint8_t ary_used = AK_ANY;
        long ary_ip = -1;
      };
      static const long NO_CLASS = -1;
      static const long MANY_CLASSES = -2;
      // what NEW_FUNC_INST makes: a lambda's captures, not an instance of any class
      static const long CLOSURE = -3;
      std::vector<Facts> facts;
      long cur_ip = 0;

      // Load-time verification: the class a reference has where it is used -- the
      // static type of the expression that made it, as far as an image spells one:
      // NEW_OBJ_INST, OBJ_INST_CAST, the method's own instance, a parameter or a
      // call's result whose name spells a qualified class, a trap's result. Not
      // more than that, deliberately. The compiler checks that a cast is between
      // related classes and then, in a call chain, does not always write the cast
      // ('worker->Call(Nil, 5000)->GetValue()->As(IntRef)->Get()' calls IntRef:Get
      // on GetValue's System.Base with no OBJ_INST_CAST between), so a valid
      // program may use a reference as any class RELATED to its static type, and
      // only an unrelated class is a verdict (Related). For the same reason a
      // claim is never carried through a local or a field (a variable's static
      // type is its declaration's, which an image does not record: 'b : Base :=
      // Dog->New()' then 'b->As(Cat)' behind a type test is valid) nor across a
      // join ('c ? cat : dog' has their common type, not both): two different
      // claims that meet are no claim. A parameter's slot is the exception: its
      // declared class is in the method's name.
      // Kept apart from the values above (a second union-find over the same ids)
      // because a cast makes the same reference a new class.
      struct ClassSet {
        long ids[1];
        uint8_t n = 0;
        // values of different or unknown types met here: nothing is claimed
        bool mixed = false;
        void Add(long cls) {
          if(mixed) {
            return;
          }
          if(n == 0) {
            ids[n++] = cls;
          }
          else if(ids[0] != cls) {
            n = 0;
            mixed = true;
          }
        }
        void Meet(const ClassSet& other) {
          if(mixed || other.mixed || n != other.n || (n == 1 && ids[0] != other.ids[0])) {
            n = 0;
            mixed = true;
          }
        }
      };
      std::vector<int> class_parent;
      std::vector<ClassSet> claims;
      enum ReqKind : uint8_t { REQ_ARGUMENT, REQ_RETURN, REQ_TRAP, REQ_STORE };
      // 'value' must be an instance of 'cls', or of one of the classes 'any_of' names
      struct ClassReq { int value; long cls; const wchar_t* any_of; long ip; uint8_t kind; };
      std::vector<ClassReq> class_reqs;
      std::unordered_map<long, std::vector<SlotType> > layouts;
      bool frame_trap = false;
      std::vector<long> param_slot_class;
      bool param_slots_read = false;

      // Load-time verification of function values. One is two words: its id (the tag,
      // a class id and a method id packed in a literal) and its closure. Nothing at
      // run time relates the method the id names to the DYN_MTHD_CALL that calls it
      // or to the closure it is called with, and the collector finds a closure's
      // layout through the id beside it. So, per value: what it was made as ('tag',
      // 'made_ip'), the other values it has met ('ring': the members of its union,
      // which the facts above only sum), and where a function type is spelled for
      // it ('spelled': a parameter's or a call's result's "m.(i,)~i").
      enum Tag : uint8_t { TAG_NONE = 0, TAG_TYPED, TAG_LIT, TAG_CLOSURE_NEW, TAG_FUNC_WORD, TAG_SELF };
      std::vector<uint8_t> tag;
      std::vector<long> made_ip;
      std::vector<int> ring;
      std::vector<const std::wstring*> spelled;
      std::vector<long> func_load_slot;
      // a use of two words as a function value: 'words' < 0 where the use names no
      // signature (a store to a local or a field)
      struct FuncUse { int id; int closure; long ip; long words; int rtrn_words; bool rtrn_float; };
      std::vector<FuncUse> func_uses;
      struct FuncStore { long slot; int id; };
      std::vector<FuncStore> func_stores;
      // the lambda each closure was made for (nullptr where two meet)
      std::unordered_map<int, StackMethod*> closure_lambda;

      // "m.(i,o.System.String,m.(i,)~i,)~f": the words its parameters take and its return
      static bool ParseFunctionType(const std::wstring& type, long& words, int& rtrn_words, bool& rtrn_float) {
        if(type.size() < 5 || type[0] != L'm' || type[1] != L'.' || type[2] != L'(') {
          return false;
        }
        words = 0;
        long depth = 1;
        size_t at = 3;
        size_t start = 3;
        for(; at < type.size() && depth > 0; ++at) {
          const wchar_t c = type[at];
          if(c == L'(') {
            ++depth;
          }
          else if(c == L')') {
            --depth;
          }
          else if(c == L',' && depth == 1) {
            words += (type[start] == L'm' && start + 1 < type.size() && type[start + 1] == L'.') ? 2 : 1;
            start = at + 1;
          }
        }
        if(depth != 0 || at >= type.size() || type[at] != L'~') {
          return false;
        }
        std::vector<SlotType> rtrn;
        ReturnWords(type.substr(at + 1), INT_TYPE, rtrn);
        rtrn_words = static_cast<int>(rtrn.size());
        rtrn_float = rtrn.size() == 1 && rtrn[0].kind == RK_FLOAT;
        return true;
      }

      // two words used as a function value: the id on top, the closure below it
      void UseFunction(int id, int closure, long ip, long words = -1, int rtrn_words = 0, bool rtrn_float = false) {
        if(!verify) {
          return;
        }
        Facts& f = facts[Find(id)];
        if(f.func_ip < 0) {
          f.func_ip = ip;
        }
        // the callee runs with the closure as its instance, and the collector marks it
        MarkRef(closure);
        func_uses.push_back({ id, closure, ip, words, rtrn_words, rtrn_float });
      }

      void UseFunctionAs(int id, int closure, long ip, const std::wstring& type) {
        long words;
        int rtrn_words;
        bool rtrn_float;
        if(ParseFunctionType(type, words, rtrn_words, rtrn_float)) {
          UseFunction(id, closure, ip, words, rtrn_words, rtrn_float);
        }
        else {
          UseFunction(id, closure, ip);
        }
      }

      // the method a function literal names: nullptr unless it is the tag, a class
      // and a method of the program that has a body
      StackMethod* FunctionOf(INT64_VALUE literal) {
        if((literal & ~(INT64_VALUE)0xFFFFFFFFLL) != (INT64_VALUE)FUNC_VALUE_TAG) {
          return nullptr;
        }
        StackClass* cls = prgm->GetClass((long)((literal >> 16) & 0xFFFF));
        StackMethod* mthd = cls ? cls->GetMethod((long)(literal & 0xFFFF)) : nullptr;
        return mthd && mthd->GetInstructionCount() > 0 ? mthd : nullptr;
      }

      bool CheckFunctions();

      int ClassFind(int v) {
        while(class_parent[v] != v) {
          class_parent[v] = class_parent[class_parent[v]];
          v = class_parent[v];
        }
        return v;
      }

      void ClassUnion(int a, int b) {
        a = ClassFind(a);
        b = ClassFind(b);
        if(a != b) {
          class_parent[b] = a;
          claims[a].Meet(claims[b]);
        }
      }

      void Claim(int v, long cls) {
        if(CLASS_RULES && verify && cls >= 0) {
          claims[ClassFind(v)].Add(cls);
        }
      }

      // an instance's fields as its class declares them, by word
      const std::vector<SlotType>& LayoutOf(long cls_id) {
        std::unordered_map<long, std::vector<SlotType> >::iterator found = layouts.find(cls_id);
        if(found != layouts.end()) {
          return found->second;
        }
        std::vector<SlotType>& layout = layouts[cls_id];
        StackClass* cls = prgm->GetClass(cls_id);
        if(cls) {
          DclrLayout(cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), layout);
        }
        return layout;
      }

      void ClassesOf(int v, ClassSet& out) {
        out = claims[ClassFind(v)];
      }

      // the class a parameter's slot is declared with (its name spells it), -1 for
      // any other local
      long ParamSlotClass(long id) {
        if(!param_slots_read) {
          // once per method: this is asked at every load and store of a local
          param_slots_read = true;
          if(context) {
            const VerifyContext::Signature& own = SignatureOf(method);
            const size_t first = method->HasAndOr() ? 1 : 0;
            const size_t words = method->GetParamCount() > 0 ? (size_t)method->GetParamCount() : 0;
            for(size_t word = 0; word < words && word < own.param_class.size(); ++word) {
              if(own.param_class[word] >= 0) {
                param_slot_class.resize(first + word + 1, -1);
                param_slot_class[first + word] = own.param_class[word];
              }
            }
          }
        }
        return id >= 0 && id < (long)param_slot_class.size() ? param_slot_class[id] : -1;
      }

      bool IsInterface(long cls) {
        if(!context) {
          return true;
        }
        if(context->is_interface.empty()) {
          const long num = prgm->GetClassNumber();
          context->is_interface.assign(num > 0 ? num : 1, 0);
          long** interfaces = prgm->GetInterfaces();
          for(long i = 0; interfaces && i < num; ++i) {
            for(long* inf = interfaces[i]; inf && *inf != INF_ENDING; ++inf) {
              if(*inf >= 0 && *inf < num) {
                context->is_interface[*inf] = 1;
              }
            }
          }
        }
        return cls < 0 || cls >= (long)context->is_interface.size() || context->is_interface[cls] != 0;
      }

      // The most memory an object of static class 'cls' can have: the object is an
      // instance of that class or, behind a cast the compiler did not write, of
      // one that inherits from it.
      INT64_VALUE FamilySize(long cls) {
        if(context->family_size.empty()) {
          const long num = prgm->GetClassNumber();
          context->family_size.assign(num > 0 ? num : 1, 0);
          long* hierarchy = prgm->GetHierarchy();
          for(long i = 0; i < num; ++i) {
            StackClass* each = prgm->GetClass(i);
            const INT64_VALUE size = each ? each->GetInstanceMemorySize() : 0;
            long at = i;
            for(long depth = 0; at >= 0 && at < num && depth <= num; ++depth) {
              if(size > context->family_size[at]) {
                context->family_size[at] = size;
              }
              at = hierarchy ? hierarchy[at] : -1;
            }
          }
        }
        return cls >= 0 && cls < (long)context->family_size.size() ? context->family_size[cls] : 0;
      }

      // Whether a reference of static class 'have' may be an instance of 'want': one
      // inherits from the other (a cast up, or a cast down the compiler did not
      // write), or either is an interface (any class's descendant may implement it)
      bool Related(long have, long want) {
        if(have == want || IsInterface(have) || IsInterface(want)) {
          return true;
        }
        return IsA(have, want) || IsA(want, have);
      }

      bool CheckClasses();      
      // a local's loads and stores, met after the pass: a local is typed by its
      // declaration alone, so what was stored says what a load can be
      struct LocalUse { long id; int value; long ip; };
      std::vector<LocalUse> local_loads;
      std::vector<LocalUse> local_stores;
      // Returns and the height each was reached at: every instruction's effect on
      // the stack is known, a trap's from its row (core/shared/trap_signatures.h)
      struct ReturnAt { long ip; long height; };
      std::vector<ReturnAt> returns;

      // field accesses: the receiver, and the context the instruction names
      struct ReceiverUse { int value; long context; long ip; long end; };
      // calls, to compare a receiver's class with the callee's once values have met
      struct CallUse { int receiver; long callee_cls; long ip; StackMethod* callee; };
      std::vector<CallUse> calls;

      bool IsA(long cls, long ancestor) {
        return ClassIsA(prgm, cls, ancestor);
      }
      std::vector<ReceiverUse> receivers;

      static uint8_t FactOf(uint8_t k) {
        switch(k) {
        case RK_LIT: return F_LIT;
        case RK_INT: return F_INT;
        case RK_FLOAT: return F_FLOAT;
        case RK_REF: return F_REF;
        default: return 0;
        }
      }

      // what made the value (a producer's fact, verification only)
      void Made(int v, uint8_t k, uint8_t shape = SHAPE_NONE) {
        if(verify) {
          Facts& f = facts[Find(v)];
          f.made |= FactOf(k);
          f.shape_made |= shape;
        }
      }

      void UseObject(int v) {
        if(verify) {
          Facts& f = facts[Find(v)];
          f.shape_used |= SHAPE_OBJECT;
          if(f.object_ip < 0) {
            f.object_ip = cur_ip;
          }
        }
      }

      void UseArray(int v) {
        if(verify) {
          Facts& f = facts[Find(v)];
          f.shape_used |= SHAPE_ARRAY;
          if(f.array_ip < 0) {
            f.array_ip = cur_ip;
          }
        }
      }

      // (an array of a known kind is an array: NEW_*_ARY did not say so before, and a
      // new Char[] passed where String:New takes a String was read as an object)
      void MadeArrayKind(int v, uint8_t made) {
        if(verify && made) {
          Facts& f = facts[Find(v)];
          if(ARRAY_KIND_RULES) {
            f.ary_made |= made;
          }
          f.shape_made |= SHAPE_ARRAY;
        }
      }

      void UseArrayKind(int v, uint8_t allowed) {
        if(ARRAY_KIND_RULES && verify && allowed && allowed != AK_ANY) {
          Facts& f = facts[Find(v)];
          f.ary_used &= allowed;
          if(f.ary_ip < 0) {
            f.ary_ip = cur_ip;
          }
        }
      }

      // entry state per instruction
      // (one arena for every instruction's entry stack: 'state_at' and 'state_size'
      // find an instruction's; a vector per instruction was an allocation each)
      std::vector<int> arena;
      std::vector<uint32_t> state_at;
      std::vector<uint32_t> state_size;
      // reused by every step and call rather than built and freed in each
      std::vector<int> work;
      std::vector<int> entry_stack;
      std::vector<SlotType> scratch_params;
      std::vector<SlotType> scratch_rtrn;
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
      struct FieldLink { int receiver; long index; int value; bool load; long context; };
      std::vector<ElemLink> elem_links;
      std::vector<FieldLink> field_links;
      std::vector<SlotType> self_fields;
      std::vector<SlotType> class_fields;
      // load-time verification: a lambda's instance is its closure, laid out by the
      // captures declared for it
      std::vector<SlotType> closure_fields;

      int NewValue(uint8_t k, uint8_t e = RK_UNKNOWN, uint8_t o = ORIGIN_NONE) {
        const int id = static_cast<int>(parent.size());
        parent.push_back(id);
        kind.push_back(k);
        use.push_back(RK_UNKNOWN);
        elem.push_back(e);
        origin.push_back(o);
        deref.push_back(0);
        lit_ip.push_back(-1);
        if(verify) {
          facts.push_back(Facts());
          facts.back().made = FactOf(k);
          class_parent.push_back(id);
          claims.push_back(ClassSet());
          tag.push_back(k == RK_UNKNOWN || k == RK_LIT ? TAG_NONE : TAG_TYPED);
          made_ip.push_back(-1);
          ring.push_back(id);
          spelled.push_back(nullptr);
          func_load_slot.push_back(-1);
        }
        return id;
      }

      int Find(int v) {
        while(parent[v] != v) {
          parent[v] = parent[parent[v]];
          v = parent[v];
        }
        return v;
      }

      // 'classes' false joins two names of one reference that may be known as
      // different classes (a cast's operand and its result)
      void Union(int a, int b, bool classes = true) {
        if(verify && classes) {
          ClassUnion(a, b);
        }
        a = Find(a);
        b = Find(b);
        if(a == b) {
          return;
        }
        parent[b] = a;
        if(verify) {
          // two rings become one
          std::swap(ring[a], ring[b]);
        }
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
        if(verify) {
          Facts& fa = facts[a];
          const Facts& fb = facts[b];
          fa.made |= fb.made;
          fa.used |= fb.used;
          fa.shape_made |= fb.shape_made;
          fa.shape_used |= fb.shape_used;
          if(fa.deref_ip < 0) fa.deref_ip = fb.deref_ip;
          if(fa.object_ip < 0) fa.object_ip = fb.object_ip;
          if(fa.array_ip < 0) fa.array_ip = fb.array_ip;
          if(fa.float_ip < 0) fa.float_ip = fb.float_ip;
          if(fa.word_ip < 0) fa.word_ip = fb.word_ip;
          fa.from_self = fa.from_self || fb.from_self;
          fa.consumed = fa.consumed || fb.consumed;
          if(fa.func_ip < 0) fa.func_ip = fb.func_ip;
          fa.ary_made |= fb.ary_made;
          fa.ary_used &= fb.ary_used;
          if(fa.ary_ip < 0) fa.ary_ip = fb.ary_ip;
          if(fa.cls_made == NO_CLASS) {
            fa.cls_made = fb.cls_made;
          }
          else if(fb.cls_made != NO_CLASS && fb.cls_made != fa.cls_made) {
            fa.cls_made = MANY_CLASSES;
          }
        }
      }

      // the value is read through as an object or array
      void MarkRef(int v) {
        v = Find(v);
        deref[v] = 1;
        if(verify && facts[v].deref_ip < 0) {
          facts[v].deref_ip = cur_ip;
        }
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
        if(verify) {
          Facts& f = facts[v];
          f.used |= FactOf(k);
          if(k == RK_FLOAT && f.float_ip < 0) {
            f.float_ip = cur_ip;
          }
          else if((k == RK_INT || k == RK_REF) && f.word_ip < 0) {
            f.word_ip = cur_ip;
          }
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
          state_at[to] = static_cast<uint32_t>(arena.size());
          state_size[to] = static_cast<uint32_t>(stack.size());
          arena.insert(arena.end(), stack.begin(), stack.end());
          andor_state[to] = andor;
          worklist.push_back(to);
          return true;
        }
        const size_t known_size = state_size[to];
        const size_t known_at = state_at[to];
        if(known_size != stack.size()) {
          return Fail(to, L"stack heights differ where control flow joins (" +
                      std::to_wstring(known_size) + L" and " + std::to_wstring(stack.size()) + L")", true);
        }
        for(size_t i = 0; i < stack.size(); ++i) {
          Union(arena[known_at + i], stack[i]);
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

      bool Pop(std::vector<int>& stack, long ip, int& v, bool consume = true) {
        if(stack.empty()) {
          return Fail(ip, L"the operand stack runs out", true);
        }
        v = stack.back();
        stack.pop_back();
        if(CLASS_RULES && verify && consume) {
          facts[Find(v)].consumed = true;
        }
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
      bool PopRef(std::vector<int>& stack, long ip, uint8_t e = RK_UNKNOWN, uint8_t allowed = AK_ANY) {
        int v;
        if(!Pop(stack, ip, v)) {
          return false;
        }
        Constrain(v, RK_REF, e);
        MarkRef(v);
        UseArray(v);
        UseArrayKind(v, allowed);
        return true;
      }

      // an array an instruction copies from, copies to or clears
      bool PopArray(std::vector<int>& stack, long ip, uint8_t allowed = AK_ANY) {
        int v;
        if(!Pop(stack, ip, v)) {
          return false;
        }
        Constrain(v, RK_REF);
        MarkRef(v);
        UseArray(v);
        UseArrayKind(v, allowed);
        return true;
      }

      // a value an instruction reads through as an object
      bool PopObject(std::vector<int>& stack, long ip) {
        int v;
        if(!Pop(stack, ip, v)) {
          return false;
        }
        Constrain(v, RK_REF);
        MarkRef(v);
        UseObject(v);
        return true;
      }

      // The receiver of a field access that is not a local's: the instance or the
      // class memory the instruction's context names. The interpreter reads the field
      // from whatever is on the stack, and the loader bounded the field's id by the
      // context it was written with.
      bool PopReceiver(std::vector<int>& stack, long ip, long context, int& receiver) {
        if(!Pop(stack, ip, receiver)) {
          return false;
        }
        MarkRef(receiver);
        if(verify) {
          const StackInstr& in = instrs[ip];
          const long width = (in.GetType() == LOAD_FUNC_VAR || in.GetType() == STOR_FUNC_VAR) ? 2 : 1;
          receivers.push_back({ receiver, context, ip, in.GetOperand() + width });
          // an instance variable is read from an object, never from an array
          if(context == INST) {
            UseObject(receiver);
          }
        }
        return true;
      }

      // "o.System.String" names a class of the program; "o.Color" (an enum) and
      // "o.T" (a generic's parameter) do not. Any array is a reference.
      bool NamesObject(const std::wstring& type) {
        if(type.empty()) {
          return false;
        }
        if(type.back() == L'*') {
          return true;
        }
        return type.size() > 2 && type[0] == L'o' && type[1] == L'.' && prgm->GetClass(type.substr(2)) != nullptr;
      }

      // The class "o.System.String" or "o.Collection.Vector<T>" spells, -1 for none.
      // Only a qualified name counts: "o.T" may be a generic's parameter and a
      // program may have a class of that name too, and nothing in an image says
      // which is meant. (An enum's name is not a class's, qualified or not.)
      long QualifiedClass(const std::wstring& type) {
        if(type.size() < 3 || type[0] != L'o' || type[1] != L'.' || type.back() == L'*') {
          return -1;
        }
        const size_t generic = type.find(L'<');
        const std::wstring name = type.substr(2, generic == std::wstring::npos ? generic : generic - 2);
        if(name.find(L'.') == std::wstring::npos || name.find(L'#') != std::wstring::npos) {
          return -1;
        }
        StackClass* cls = prgm->GetClass(name);
        return cls ? cls->GetId() : -1;
      }

      // which of a method's parameter words and whether its return hold objects,
      // read once from its name ("Class:Method:o.System.String*,i,m.(i,)~i,")
      const VerifyContext::Signature& SignatureOf(StackMethod* m) {
        std::unordered_map<StackMethod*, VerifyContext::Signature>::iterator found = context->signatures.find(m);
        if(found != context->signatures.end()) {
          return found->second;
        }
        VerifyContext::Signature& sig = context->signatures[m];
        const std::wstring& name = m->GetName();
        const size_t first = name.find(L':');
        const size_t second = first == std::wstring::npos ? first : name.find(L':', first + 1);
        if(second != std::wstring::npos) {
          long depth = 0;
          size_t start = second + 1;
          for(size_t i = second + 1; i < name.size(); ++i) {
            const wchar_t c = name[i];
            if(c == L'(') {
              ++depth;
            }
            else if(c == L')') {
              --depth;
            }
            else if(c == L',' && depth == 0) {
              const std::wstring type = name.substr(start, i - start);
              if(type.size() > 1 && type[0] == L'm' && type[1] == L'.') {
                // a function value: its closure, then its id
                sig.object_param.push_back(0);
                sig.object_param.push_back(0);
                sig.param_class.push_back(-1);
                sig.param_class.push_back(-1);
                sig.param_function.push_back(type);
                sig.param_function.push_back(type);
              }
              else {
                sig.object_param.push_back(NamesObject(type) ? 1 : 0);
                sig.param_class.push_back(QualifiedClass(type));
                sig.param_function.push_back(std::wstring());
              }
              start = i + 1;
            }
          }
        }
        const std::wstring& rtrn = m->GetReturnName();
        sig.object_return = !(rtrn.size() > 1 && rtrn[0] == L'm' && rtrn[1] == L'.') && NamesObject(rtrn);
        sig.return_class = QualifiedClass(rtrn);
        return sig;
      }

      // a value of a declared type: a local, a field, a parameter or a return
      int NewSlotValue(const SlotType& slot) {
        const int value = NewValue(slot.kind, slot.elem);
        if(verify) {
          facts[value].shape_made = slot.shape;
          facts[value].ary_made = ARRAY_KIND_RULES ? slot.ary : 0;
          if(slot.part == 1) {
            facts[value].made = F_FUNC;
          }
          else if(slot.part == 2) {
            tag[value] = TAG_FUNC_WORD;
          }
        }
        return value;
      }

      // a value passed, returned or stored where a declaration types it
      void ConstrainSlot(int v, const SlotType& slot) {
        Constrain(v, slot.kind, slot.elem);
        if(slot.shape == SHAPE_OBJECT) {
          UseObject(v);
        }
        else if(slot.shape == SHAPE_ARRAY) {
          UseArray(v);
          UseArrayKind(v, slot.ary);
        }
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
          stack.push_back(NewSlotValue(words[i]));
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

      // "A|B|C" holds 'name' (a trap's contexts: the methods it may stand in)
      static bool ListHolds(const wchar_t* list, const std::wstring& name) {
        const wchar_t* at = list;
        while(*at) {
          const wchar_t* end = at;
          while(*end && *end != L'|') {
            ++end;
          }
          if(static_cast<size_t>(end - at) == name.size() && name.compare(0, name.size(), at, name.size()) == 0) {
            return true;
          }
          at = *end ? end + 1 : end;
        }
        return false;
      }

      // the arrays a trap's word may be, as ArrayKind bits
      static uint8_t TrapArrays(unsigned char words) {
        using namespace trap_signatures;
        return static_cast<uint8_t>(((words & W_BYTE_ARY) ? AK_BYTE : 0) | ((words & W_CHAR_ARY) ? AK_CHAR : 0) |
                                    ((words & (W_INT_ARY | W_OBJ_ARY)) ? AK_WORD : 0) | ((words & W_FLOAT_ARY) ? AK_FLOAT : 0));
      }

      // a word a trap takes: the kind its row names (core/shared/trap_signatures.h)
      void ConstrainTrapWord(int v, const trap_signatures::Arg& arg) {
        using namespace trap_signatures;
        const unsigned char words = arg.words;
        if(words == W_ANY) {
          return;
        }
        const unsigned char arrays = W_BYTE_ARY | W_CHAR_ARY | W_INT_ARY | W_FLOAT_ARY | W_OBJ_ARY;
        const bool reference = (words & (arrays | W_OBJ)) != 0;
        if(words == W_FLOAT) {
          Constrain(v, RK_FLOAT);
        }
        else if(words == W_INT) {
          Constrain(v, RK_INT);
        }
        else if(reference && !(words & (W_INT | W_FLOAT))) {
          // the handler reads through it
          Constrain(v, RK_REF);
          MarkRef(v);
          if(!(words & W_OBJ)) {
            UseArray(v);
            UseArrayKind(v, TrapArrays(words));
          }
          else if(!(words & arrays)) {
            UseObject(v);
            if(arg.classes) {
              if(CLASS_RULES) class_reqs.push_back({ v, -1, arg.classes, cur_ip, REQ_TRAP });
            }
          }
        }
      }

      // the word a trap leaves
      int NewTrapResult(const trap_signatures::Arg& result) {
        using namespace trap_signatures;
        const int value = NewValue(RK_UNKNOWN);
        const unsigned char words = result.words;
        const unsigned char arrays = W_BYTE_ARY | W_CHAR_ARY | W_INT_ARY | W_FLOAT_ARY | W_OBJ_ARY;
        if(words == W_ANY) {
          return value;
        }
        if(words == W_FLOAT) {
          Made(value, RK_FLOAT);
        }
        else if(words == W_INT) {
          Made(value, RK_INT);
        }
        else if(!(words & (W_INT | W_FLOAT))) {
          if(!(words & W_OBJ)) {
            Made(value, RK_REF, SHAPE_ARRAY);
            // one array named: the result is that one
            const uint8_t made = TrapArrays(words);
            if(made == AK_BYTE || made == AK_CHAR || made == AK_WORD || made == AK_FLOAT) {
              MadeArrayKind(value, made);
            }
          }
          else if(!(words & arrays)) {
            Made(value, RK_REF, SHAPE_OBJECT);
            // one class named: the result is an instance of it
            if(result.classes && !wcschr(result.classes, L'|')) {
              StackClass* cls = prgm->GetClass(result.classes);
              if(cls) {
                Claim(value, cls->GetId());
              }
            }
          }
        }
        return value;
      }

      // Load-time verification of TRAP and TRAP_RTRN. A trap's handler pops what its
      // own id says, whatever the instruction's count is, and pushes a result or
      // not: the row is that declaration. The id is the literal just below (the
      // loader checked it is one, and no jump lands between the two).
      bool StepTrap(std::vector<int>& stack, long ip, bool rtrn, long count) {
        const trap_signatures::Row* row = nullptr;
        if(ip > 0 && instrs[ip - 1].GetType() == LOAD_INT_LIT) {
          row = trap_signatures::Find(instrs[ip - 1].GetInt64Operand());
        }
        if(!row) {
          return Fail(ip, L"a trap id the compiler does not write", true);
        }
        if(row->rtrn != rtrn) {
          return Fail(ip, std::wstring(L"trap ") + row->name + (row->rtrn ? L" is written TRAP_RTRN" : L" is written TRAP"), true);
        }
        if(row->count >= 0 && row->count != count) {
          return Fail(ip, std::wstring(L"trap ") + row->name + L" takes " + std::to_wstring(row->count) +
                      L" words, not " + std::to_wstring(count), true);
        }
        // a handler that reads its caller's frame or instance is given them by the
        // library method the directive was written in, and by no other
        if(row->instance) {
          frame_trap = true;
        }
        if(row->contexts && !ListHolds(row->contexts, method->GetName())) {
          return Fail(ip, std::wstring(L"trap ") + row->name + L" outside the library method whose frame it reads", true);
        }
        int v;
        // the id
        if(!Pop(stack, ip, v)) return false;
        if(row->count < 0) {
          // a literal array of strings: one word for each element, then the array,
          // which comes back (Loader::CheckLiteralCopy checked the shape whole)
          int array = -1;
          for(long i = 1; i < count; ++i) {
            if(!Pop(stack, ip, v)) return false;
            if(i == 1) {
              array = v;
            }
          }
          stack.push_back(array >= 0 ? array : NewTrapResult(row->result));
          return true;
        }
        int first = -1;
        for(long i = count - 2; i >= 0; --i) {
          if(!Pop(stack, ip, v)) return false;
          ConstrainTrapWord(v, row->args[i]);
          if(i == 0) {
            first = v;
          }
        }
        if(row->pushes) {
          // a literal copy hands back the array it filled
          const long long id = row->id;
          const bool copy = id == instructions::CPY_CHAR_STR_ARY || id == instructions::CPY_INT_STR_ARY ||
                            id == instructions::CPY_BOOL_STR_ARY || id == instructions::CPY_BYTE_STR_ARY ||
                            id == instructions::CPY_FLOAT_STR_ARY;
          stack.push_back(copy && first >= 0 ? first : NewTrapResult(row->result));
        }
        return true;
      }

      // LOAD_FUNC_VAR and STOR_FUNC_VAR take two words of the frame: those of a local
      // declared a function value, where the method declares the slot at all
      bool FunctionLocal(long ip, long id) {
        if(verify && id >= 0 && id < (long)locals.size() && !AndOrLocal(id) && locals[id].part != 1) {
          return Fail(ip, L"a function value read from or stored to a local that is not one", true);
        }
        return true;
      }

      // A Float is not read from or stored to a local declared a reference or a
      // function value (the collector reads those words as addresses), and a
      // Float local is read and written as a Float. An Int local may hold one: the
      // temporary of an inlined 'a ? b : c' over Floats is declared Int.
      bool FloatLocal(long ip, long id, bool as_float) {
        if(!verify || id < 0 || id >= (long)locals.size() || AndOrLocal(id)) {
          return true;
        }
        if(as_float && (locals[id].kind == RK_REF || locals[id].part != 0)) {
          return Fail(ip, L"a Float read from or stored to a local that holds a reference", true);
        }
        if(!as_float && locals[id].kind == RK_FLOAT) {
          return Fail(ip, L"an integer or a reference read from or stored to a Float local", true);
        }
        return true;
      }

      bool Step(long ip);
      void ResolveLinks();
      bool CheckFacts();

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
      Builder(StackProgram* p, StackMethod* m, RefMap* r, bool v = false, VerifyContext* c = nullptr) :
        prgm(p), method(m), verify(v), context(c), map(r) {
        instrs = m->GetInstructions();
        count = m->GetInstructionCount();
      }

      // The same builder for another method: every table emptied, its storage kept.
      // ('layouts' is a class's declarations and stays.) A member added to this
      // class is added here, or one method's facts reach the next.
      void Reset(StackMethod* m, RefMap* r) {
        method = m;
        map = r;
        instrs = m->GetInstructions();
        count = m->GetInstructionCount();
        cur_ip = 0;
        frame_trap = false;
        param_slots_read = false;
        min_height = 0;
        parent.clear();
        kind.clear();
        use.clear();
        elem.clear();
        origin.clear();
        deref.clear();
        lit_ip.clear();
        facts.clear();
        class_parent.clear();
        claims.clear();
        class_reqs.clear();
        param_slot_class.clear();
        tag.clear();
        made_ip.clear();
        ring.clear();
        spelled.clear();
        func_load_slot.clear();
        func_uses.clear();
        func_stores.clear();
        closure_lambda.clear();
        local_loads.clear();
        local_stores.clear();
        returns.clear();
        calls.clear();
        receivers.clear();
        arena.clear();
        state_at.clear();
        state_size.clear();
        work.clear();
        entry_stack.clear();
        scratch_params.clear();
        scratch_rtrn.clear();
        andor_state.clear();
        andor_joins.clear();
        edges.clear();
        seen.clear();
        worklist.clear();
        locals.clear();
        local_is_andor.clear();
        elem_links.clear();
        field_links.clear();
        self_fields.clear();
        class_fields.clear();
        closure_fields.clear();
      }

      void Run();
    };

    inline bool Builder::Step(long ip) {
      std::vector<int>& stack = work;
      stack.assign(arena.begin() + state_at[ip], arena.begin() + state_at[ip] + state_size[ip]);
      int andor = andor_state[ip];
      StackInstr* instr = &instrs[ip];
      const long op1 = instr->GetOperand();
      const long op2 = instr->GetOperand2();
      const int entry_height = static_cast<int>(stack.size());
      bool falls_through = true;
      SlotType slot;
      int v;
      min_height = stack.size();
      cur_ip = ip;

      switch(instr->GetType()) {
      case LOAD_INT_LIT: {
        const int lit = NewValue(RK_LIT);
        if(instr->GetInt64Operand() != 0) {
          lit_ip[lit] = ip;
        }
        if(verify) {
          tag[lit] = TAG_LIT;
          made_ip[lit] = ip;
        }
        stack.push_back(lit);
      }
        break;

      case LOAD_CHAR_LIT:
        stack.push_back(NewValue(RK_INT));
        break;

      case NAN_INT: case INF_INT: case NEG_INF_INT:
        // a float's bits that the caller holds as an Int (Int->NaN()): the maps type
        // the word as a float, and verification takes it for either
        v = NewValue(RK_FLOAT);
        if(verify) {
          facts[v].made = 0;
        }
        stack.push_back(v);
        break;

      case LOAD_FLOAT_LIT:
      case NAN_FLOAT: case INF_FLOAT: case NEG_INF_FLOAT:
      case RAND_FLOAT:
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case LOAD_LOCL_INT_VAR:
        if(!LocalType(ip, op1, slot) || !FloatLocal(ip, op1, false)) return false;
        if(AndOrLocal(op1)) {
          stack.push_back(andor);
        }
        else {
          const int value = NewSlotValue(slot);
          if(verify) {
            // A local's declaration does not say what a load is: the temporary of an
            // inlined 'a ? b : c' over objects is declared an Int. What was stored
            // does (CheckFacts), so only a reference's declaration counts here.
            if(slot.kind != RK_REF) {
              facts[value].made = 0;
            }
            local_loads.push_back({ op1, value, ip });
            if(CLASS_RULES) {
              Claim(value, ParamSlotClass(op1));
            }
            // A word of the frame no declaration covers is not a root: the collector
            // never marks or moves what it holds, so it is not read through
            if(op1 >= (long)locals.size()) {
              facts[value].made = F_INT;
            }
          }
          stack.push_back(value);
        }
        break;

      case STOR_LOCL_INT_VAR:
      case COPY_LOCL_INT_VAR:
        if(!LocalType(ip, op1, slot) || !FloatLocal(ip, op1, false)) return false;
        if(instr->GetType() == STOR_LOCL_INT_VAR) {
          // (kept in a local, the instance is not read yet: the inliner parks a
          // function's caller's instance in one to hand it on)
          if(!Pop(stack, ip, v, false)) return false;
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
          UseArrayKind(v, slot.ary);
          if(verify) {
            local_stores.push_back({ op1, v, ip });
            const long declared = CLASS_RULES ? ParamSlotClass(op1) : -1;
            if(declared >= 0) {
              if(CLASS_RULES) class_reqs.push_back({ v, declared, nullptr, ip, REQ_STORE });
            }
          }
        }
        break;

      case LOAD_CLS_INST_INT_VAR: {
        int receiver;
        if(!PopReceiver(stack, ip, op2, receiver)) return false;
        const int value = NewValue(RK_UNKNOWN);
        field_links.push_back({ receiver, op1, value, true, op2 });
        stack.push_back(value);
      }
        break;

      case STOR_CLS_INST_INT_VAR: {
        int receiver;
        if(!PopReceiver(stack, ip, op2, receiver) || !Pop(stack, ip, v)) return false;
        field_links.push_back({ receiver, op1, v, false, op2 });
      }
        break;

      case COPY_CLS_INST_INT_VAR: {
        int receiver;
        if(!PopReceiver(stack, ip, op2, receiver)) return false;
        if(stack.empty()) return Fail(ip, L"the operand stack runs out", true);
        field_links.push_back({ receiver, op1, stack.back(), false, op2 });
      }
        break;

      case LOAD_FLOAT_VAR:
        if(op2 == LOCL && !FloatLocal(ip, op1, true)) return false;
        if(op2 != LOCL && !PopReceiver(stack, ip, op2, v)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case STOR_FLOAT_VAR:
        if(op2 == LOCL && !FloatLocal(ip, op1, true)) return false;
        if(op2 != LOCL && !PopReceiver(stack, ip, op2, v)) return false;
        if(!PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case COPY_FLOAT_VAR:
        if(op2 == LOCL && !FloatLocal(ip, op1, true)) return false;
        if(op2 != LOCL && !PopReceiver(stack, ip, op2, v)) return false;
        if(stack.empty()) return Fail(ip, L"the operand stack runs out", true);
        Constrain(stack.back(), RK_FLOAT);
        break;

      case LOAD_FUNC_VAR: {
        if(op2 != LOCL && !PopReceiver(stack, ip, op2, v)) return false;
        if(op2 == LOCL && !FunctionLocal(ip, op1)) return false;
        const int closure = NewValue(RK_REF);
        const int id = NewValue(RK_INT);
        if(verify) {
          tag[closure] = TAG_FUNC_WORD;
          facts[id].made = F_FUNC;
          if(op2 == LOCL) {
            func_load_slot[id] = op1;
          }
        }
        stack.push_back(closure);
        stack.push_back(id);
      }
        break;

      case STOR_FUNC_VAR: {
        if(op2 != LOCL && !PopReceiver(stack, ip, op2, v)) return false;
        if(op2 == LOCL && !FunctionLocal(ip, op1)) return false;
        int id;
        int closure;
        if(!Pop(stack, ip, id) || !Pop(stack, ip, closure)) return false;
        Constrain(id, RK_INT);
        Constrain(closure, RK_REF);
        UseFunction(id, closure, ip);
        if(verify && op2 == LOCL) {
          func_stores.push_back({ op1, id });
        }
      }
        break;

      case LOAD_CLS_MEM:
        // class memory is not a heap object: never marked, never moved
        v = NewValue(RK_INT, RK_UNKNOWN, ORIGIN_CLS);
        if(verify) {
          facts[v].made = F_CLS;
        }
        stack.push_back(v);
        break;

      case LOAD_INST_MEM:
        v = NewValue(RK_REF, RK_UNKNOWN, ORIGIN_SELF);
        if(verify) {
          tag[v] = TAG_SELF;
          facts[v].from_self = true;
          // (a lambda's instance is its closure)
          if(!method->IsLambda() && method->GetClass()) {
            Claim(v, method->GetClass()->GetId());
          }
        }
        stack.push_back(v);
        break;

      case LOAD_BYTE_ARY_ELM:
      case LOAD_CHAR_ARY_ELM:
        if(!PopRef(stack, ip, RK_UNKNOWN, instr->GetType() == LOAD_BYTE_ARY_ELM ? AK_BYTE : AK_CHAR) ||
           !PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case LOAD_FLOAT_ARY_ELM:
        if(!PopRef(stack, ip, RK_FLOAT, AK_FLOAT) || !PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case LOAD_INT_ARY_ELM: {
        int array;
        if(!Pop(stack, ip, array)) return false;
        Constrain(array, RK_REF);
        MarkRef(array);
        UseArray(array);
        UseArrayKind(array, AK_WORD);
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        const int value = NewValue(RK_UNKNOWN);
        elem_links.push_back({ array, value, true });
        stack.push_back(value);
      }
        break;

      case STOR_BYTE_ARY_ELM:
      case STOR_CHAR_ARY_ELM:
        if(!PopRef(stack, ip, RK_UNKNOWN, instr->GetType() == STOR_BYTE_ARY_ELM ? AK_BYTE : AK_CHAR) ||
           !PopN(stack, ip, op1, RK_INT) || !PopKind(stack, ip, RK_INT)) return false;
        break;

      case STOR_FLOAT_ARY_ELM:
        if(!PopRef(stack, ip, RK_FLOAT, AK_FLOAT) || !PopN(stack, ip, op1, RK_INT) || !PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case STOR_INT_ARY_ELM: {
        int array;
        if(!Pop(stack, ip, array)) return false;
        Constrain(array, RK_REF);
        MarkRef(array);
        UseArray(array);
        UseArrayKind(array, AK_WORD);
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
        if(!PopArray(stack, ip, AK_CHAR) || !PopKind(stack, ip, RK_INT)) return false;
        stack.push_back(NewValue(RK_INT));
        break;

      case S2F:
        if(!PopArray(stack, ip, AK_CHAR)) return false;
        stack.push_back(NewValue(RK_FLOAT));
        break;

      case I2S:
        if(!PopArray(stack, ip, AK_CHAR) || !PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_INT)) return false;
        break;

      case F2S:
        if(!PopArray(stack, ip, AK_CHAR) || !PopKind(stack, ip, RK_FLOAT)) return false;
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
        // (handing the instance on to a method that is not virtual is not yet a use
        // of it: a function is called with whatever instance its caller has)
        if(!Pop(stack, ip, receiver_value, !verify || callee->IsVirtual())) return false;
        MarkRef(receiver_value);
        if(verify) {
          // (not marked as an object: a primitive array's helper function takes the
          // array as its receiver word, 'values->Size()' before it is inlined)
          calls.push_back({ receiver_value, op1, ip, callee });
        }
        std::vector<SlotType>& params = scratch_params;
        ParamWords(callee, params);
        const VerifyContext::Signature* sig = (verify && context) ? &SignatureOf(callee) : nullptr;
        int function_id = -1;
        for(size_t i = params.size(); i > 0; --i) {
          if(!Pop(stack, ip, v)) return false;
          ConstrainSlot(v, params[i - 1]);
          // a function value: its id was on top, its closure is this word
          if(verify && params[i - 1].part == 1) {
            function_id = v;
          }
          else if(verify && params[i - 1].part == 2 && function_id >= 0) {
            if(sig && i - 1 < sig->param_function.size() && !sig->param_function[i - 1].empty()) {
              UseFunctionAs(function_id, v, ip, sig->param_function[i - 1]);
            }
            else {
              UseFunction(function_id, v, ip);
            }
            function_id = -1;
          }
          // the callee reads a class-typed or array parameter through
          if(sig && i - 1 < sig->object_param.size() && sig->object_param[i - 1]) {
            MarkRef(v);
          }
          if(sig && i - 1 < sig->param_class.size() && sig->param_class[i - 1] >= 0) {
            if(CLASS_RULES) class_reqs.push_back({ v, sig->param_class[i - 1], nullptr, ip, REQ_ARGUMENT });
          }
        }
        std::vector<SlotType>& rtrn = scratch_rtrn;
        ReturnWords(callee->GetReturnName(), callee->GetReturn(), rtrn);
        // a placeholder body returns without the value its type names, and the
        // caller would go on to read a word nobody pushed
        if(verify && !rtrn.empty() && callee->GetInstructionCount() == 1 && callee->GetInstruction(0)->GetType() == RTRN) {
          return Fail(ip, L"a call to a method whose body returns no value where its type names one", true);
        }
        PushWords(stack, rtrn);
        if(sig && rtrn.size() == 1 && sig->return_class >= 0) {
          Claim(stack.back(), sig->return_class);
        }
        if(verify && rtrn.size() == 2 && rtrn[1].part == 1) {
          spelled[stack.back()] = &callee->GetReturnName();
        }
      }
        break;

      case DYN_MTHD_CALL:
      case DYN_MTHD_CALL_JIT: {
        // the func-ref's id on top, its closure below, then operand arguments
        int id;
        if(!Pop(stack, ip, id) || !Pop(stack, ip, v)) return false;
        Constrain(id, RK_INT);
        // the closure the callee runs with as its instance
        Constrain(v, RK_REF);
        MarkRef(v);
        if(!PopN(stack, ip, op1)) return false;
        std::vector<SlotType>& rtrn = scratch_rtrn;
        ReturnWords(L"", (MemoryType)op2, rtrn);
        UseFunction(id, v, ip, op1, static_cast<int>(rtrn.size()), rtrn.size() == 1 && rtrn[0].kind == RK_FLOAT);
        PushWords(stack, rtrn);
      }
        break;

      case JMP:
        if(op2 >= 0 && !PopKind(stack, ip, RK_INT)) return false;
        if(!Flow(ip, instr->JumpTargetIndex(), stack, andor)) return false;
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
        if(!Flow(ip, instr->JumpTargetIndex(), stack, andor)) return false;
        break;

      case RTRN: {
        std::vector<SlotType>& rtrn = scratch_rtrn;
        ReturnWords(method->GetReturnName(), method->GetReturn(), rtrn);
        if(verify) {
          returns.push_back({ ip, (long)stack.size() });
        }
        if(stack.size() >= rtrn.size()) {
          for(size_t i = 0; i < rtrn.size(); ++i) {
            ConstrainSlot(stack[stack.size() - rtrn.size() + i], rtrn[i]);
          }
          if(verify) {
            for(size_t i = 0; i < rtrn.size(); ++i) {
              facts[Find(stack[stack.size() - rtrn.size() + i])].consumed = true;
            }
          }
          if(verify && rtrn.size() == 2 && rtrn[1].part == 1) {
            UseFunctionAs(stack[stack.size() - 1], stack[stack.size() - 2], ip, method->GetReturnName());
          }
          if(verify && context && rtrn.size() == 1) {
            const VerifyContext::Signature& own = SignatureOf(method);
            // the caller reads a class-typed or array result through
            if(own.object_return) {
              MarkRef(stack.back());
            }
            if(own.return_class >= 0) {
              if(CLASS_RULES) class_reqs.push_back({ stack.back(), own.return_class, nullptr, ip, REQ_RETURN });
            }
          }
        }
        falls_through = false;
      }
        break;

      case NEW_BYTE_ARY:
      case NEW_CHAR_ARY:
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF, RK_INT));
        MadeArrayKind(stack.back(), instr->GetType() == NEW_BYTE_ARY ? AK_BYTE : AK_CHAR);
        break;

      case NEW_INT_ARY:
        // an Int[] (GC roots phase 6): its elements are Ints
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF, RK_INT));
        MadeArrayKind(stack.back(), AK_WORD);
        break;

      case NEW_OBJ_ARY:
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF));
        MadeArrayKind(stack.back(), AK_WORD);
        break;

      case NEW_FLOAT_ARY:
        if(!PopN(stack, ip, op1, RK_INT)) return false;
        stack.push_back(NewValue(RK_REF, RK_FLOAT));
        MadeArrayKind(stack.back(), AK_FLOAT);
        break;

      case NEW_OBJ_INST:
        v = NewValue(RK_REF);
        if(verify) {
          facts[v].shape_made = SHAPE_OBJECT;
          facts[v].cls_made = op1;
          Claim(v, op1);
        }
        stack.push_back(v);
        break;

      case NEW_FUNC_INST:
        v = NewValue(RK_REF);
        if(verify) {
          facts[v].cls_made = CLOSURE;
          tag[v] = TAG_CLOSURE_NEW;
          made_ip[v] = ip;
        }
        stack.push_back(v);
        break;

      case CPY_BYTE_ARY: case CPY_CHAR_ARY: case CPY_INT_ARY: case CPY_FLOAT_ARY: {
        const InstructionType copy = instr->GetType();
        const uint8_t allowed = copy == CPY_BYTE_ARY ? AK_BYTE : copy == CPY_CHAR_ARY ? AK_CHAR :
                                copy == CPY_FLOAT_ARY ? AK_FLOAT : AK_WORD;
        // length, source offset, source, destination offset, destination
        if(!PopKind(stack, ip, RK_INT) || !PopKind(stack, ip, RK_INT) || !PopArray(stack, ip, allowed) ||
           !PopKind(stack, ip, RK_INT) || !PopArray(stack, ip, allowed)) return false;
        stack.push_back(NewValue(RK_INT));
      }
        break;

      case ZERO_BYTE_ARY: case ZERO_CHAR_ARY: case ZERO_INT_ARY: case ZERO_FLOAT_ARY: {
        const InstructionType zero = instr->GetType();
        if(!PopArray(stack, ip, zero == ZERO_BYTE_ARY ? AK_BYTE : zero == ZERO_CHAR_ARY ? AK_CHAR :
                                zero == ZERO_FLOAT_ARY ? AK_FLOAT : AK_WORD)) return false;
      }
        break;

      case OBJ_INST_CAST: {
        // the same reference comes back (or Nil)
        if(!Pop(stack, ip, v)) return false;
        Constrain(v, RK_REF);
        // the cast reads the object's class
        MarkRef(v);
        if(verify) {
          // the same reference, and from here an instance of the class the cast
          // names: the interpreter and the compiled code stop the program otherwise
          const int cast = NewValue(RK_UNKNOWN);
          Union(v, cast, false);
          Claim(cast, op1);
          stack.push_back(cast);
        }
        else {
          stack.push_back(v);
        }
      }
        break;

      case OBJ_TYPE_OF:
        if(!Pop(stack, ip, v)) return false;
        Constrain(v, RK_REF);
        MarkRef(v);
        stack.push_back(NewValue(RK_INT));
        break;

      case TRAP: {
        if(verify) {
          if(!StepTrap(stack, ip, false, op1)) return false;
          break;
        }
        // the trap id and its arguments, none typed here
        if(!PopN(stack, ip, op1)) return false;
        // A TRAP pushes nothing, as the JIT models it, except where its result is
        // the method's: a trap right before the RTRN of a method that returns a
        // value (Deserializer->ReadInt is LOAD_INT_LIT id; TRAP args=1; RTRN, and
        // the trap pushes the Int). Such a method never compiles: the JIT finds
        // nothing to return. The census of the suite's traps (#1457) found every
        // value-pushing TRAP in this position and none elsewhere.
        if(ip + 1 < count && instrs[ip + 1].GetType() == RTRN) {
          std::vector<SlotType>& rtrn = scratch_rtrn;
          ReturnWords(method->GetReturnName(), method->GetReturn(), rtrn);
          while(stack.size() < rtrn.size()) {
            stack.push_back(NewValue(RK_UNKNOWN));
          }
        }
      }
        break;

      case TRAP_RTRN:
        if(verify) {
          if(!StepTrap(stack, ip, true, op1)) return false;
          break;
        }
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
        if(!Pop(stack, ip, v, false)) return false;
        break;

      case POP_FLOAT:
        if(!PopKind(stack, ip, RK_FLOAT)) return false;
        break;

      case THREAD_SLEEP:
        if(!PopKind(stack, ip, RK_INT)) return false;
        break;

      case CRITICAL_START:
      case CRITICAL_END:
        if(!PopObject(stack, ip)) return false;
        break;

      case JMP_TABLE: {
        if(!PopKind(stack, ip, RK_INT)) return false;
        const long range = op2;
        if(range < 0 || ip + range >= count) {
          return Fail(ip, L"a select table that runs past the method");
        }
        for(long i = 1; i <= range; ++i) {
          if(!Flow(ip, instrs[ip + i].JumpTargetIndex(), stack, andor)) return false;
        }
        if(!Flow(ip, instr->JumpDefaultTargetIndex(), stack, andor)) return false;
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
              if(verify && fields == &self_fields) {
                Made(value, (*fields)[link.index].kind, (*fields)[link.index].shape);
                MadeArrayKind(value, (*fields)[link.index].ary);
              }
            }
            else {
              Constrain(value, (*fields)[link.index].kind, (*fields)[link.index].elem);
              if(fields == &self_fields) {
                UseArrayKind(value, (*fields)[link.index].ary);
              }
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

    // Load-time verification: each value's makers against its uses, once every path
    // has been walked and the values that meet have been joined. False refuses the
    // method.
    inline bool Builder::CheckFacts() {
      // what a lambda loads from its closure is what the capture was declared as
      if(method->IsLambda()) {
        for(size_t i = 0; i < field_links.size(); ++i) {
          const FieldLink& link = field_links[i];
          if(link.load && origin[Find(link.receiver)] == ORIGIN_SELF && link.index >= 0 && link.index < (long)closure_fields.size()) {
            Made(link.value, closure_fields[link.index].kind, closure_fields[link.index].shape);
            MadeArrayKind(link.value, closure_fields[link.index].ary);
          }
        }
      }

      for(size_t i = 0; i < parent.size(); ++i) {
        const int v = static_cast<int>(i);
        if(Find(v) != v) {
          continue;
        }
        const Facts& f = facts[v];
        // arithmetic, a comparison or a float is not an address
        if(deref[v] && (f.made & (F_INT | F_FLOAT | F_FUNC))) {
          return Fail(f.deref_ip, L"a number used as a reference", true);
        }
        if(f.func_ip >= 0 && (f.made & (F_INT | F_FLOAT | F_REF | F_CLS))) {
          return Fail(f.func_ip, L"a number or a reference used as a function value", true);
        }
        if((f.made & F_FLOAT) && (f.used & (F_INT | F_REF))) {
          return Fail(f.word_ip, L"a float used as an integer or a reference", true);
        }
        // (an integer literal may be: the zero an inlined 'try' leaves for a Float)
        if((f.made & (F_INT | F_REF | F_CLS | F_FUNC)) && (f.used & F_FLOAT)) {
          return Fail(f.float_ip, L"an integer or a reference used as a float", true);
        }
        if((f.shape_made & SHAPE_OBJECT) && (f.shape_used & SHAPE_ARRAY)) {
          return Fail(f.array_ip, L"an object used as an array", true);
        }
        if((f.shape_made & SHAPE_ARRAY) && (f.shape_used & SHAPE_OBJECT)) {
          return Fail(f.object_ip, L"an array used as an object", true);
        }
        if(f.ary_made & ~f.ary_used) {
          return Fail(f.ary_ip, L"an array of one element type used as an array of another", true);
        }
        if((f.made & F_CLS) && (f.shape_used || (f.used & F_REF))) {
          return Fail(f.array_ip >= 0 ? f.array_ip : (f.object_ip >= 0 ? f.object_ip : f.word_ip),
                      L"class memory used as an object or an array", true);
        }
      }

      // The caller goes on with the stack it had, less the arguments, plus the words
      // the method's return type names: nothing else tells it what the method left.
      // A return at another height shifts every word the caller reads afterwards.
      // (A body that is a lone RTRN is a compiler intrinsic's placeholder --
      // System.Base:Try -- which nothing calls: MTHD_CALL refuses a call to one,
      // and a virtual call cannot resolve to one, StackInterpreter::ResolveVirtualTarget.)
      if(count > 1) {
        std::vector<SlotType> rtrn;
        ReturnWords(method->GetReturnName(), method->GetReturn(), rtrn);
        for(size_t i = 0; i < returns.size(); ++i) {
          if(returns[i].height != (long)rtrn.size()) {
            return Fail(returns[i].ip, L"a return with " + std::to_wstring(returns[i].height) +
                        L" words on the operand stack where the method returns " + std::to_wstring(rtrn.size()), true);
          }
        }
      }

      if(!CheckFunctions()) {
        return false;
      }

      // a field's receiver is what its context says: class memory for CLS (class
      // memory read as an instance was refused above: the loader bounded the id
      // by the context)
      for(size_t i = 0; i < receivers.size(); ++i) {
        const Facts& f = facts[Find(receivers[i].value)];
        if(receivers[i].context == CLS) {
          if(f.made != F_CLS) {
            return Fail(receivers[i].ip, L"a class variable read from something other than class memory", true);
          }
        }
        else {
          const int r = Find(receivers[i].value);
          // a lambda's own instance is its closure: the captures and no more
          if(f.cls_made == NO_CLASS && origin[r] == ORIGIN_SELF && method->IsLambda() &&
             receivers[i].end > (long)closure_fields.size()) {
            return Fail(receivers[i].ip, L"a captured variable beyond the lambda's closure", true);
          }
          // An object NEW_OBJ_INST just made, and the method's own instance, hold
          // their class's fields and no more (the loader bounded the id by the
          // largest object of any class), each of the kind the class declares: a
          // Float is not read from an Int's or a reference's word, nor a function
          // value from anything else. (Any other reference may be an instance of
          // a class below its static type: see ClassSet.)
          long cls = f.cls_made;
          if(cls == CLOSURE) {
            // a new closure: no word past the bytes it was made with, and each
            // capture as the lambda it was made for declares it
            const InstructionType access = instrs[receivers[i].ip].GetType();
            const bool as_float = access == LOAD_FLOAT_VAR || access == STOR_FLOAT_VAR || access == COPY_FLOAT_VAR;
            const bool as_func = access == LOAD_FUNC_VAR || access == STOR_FUNC_VAR;
            const long index = instrs[receivers[i].ip].GetOperand();
            int member = r;
            do {
              if(tag[member] == TAG_CLOSURE_NEW && receivers[i].end * (long)sizeof(size_t) > instrs[made_ip[member]].GetOperand()) {
                return Fail(receivers[i].ip, L"a captured variable beyond the closure", true);
              }
              member = ring[member];
            } while(member != r);
            std::unordered_map<int, StackMethod*>::iterator made_for = closure_lambda.find(r);
            if(made_for != closure_lambda.end() && made_for->second && made_for->second->GetClass()) {
              const std::pair<int, StackDclr**> captures =
                made_for->second->GetClass()->GetClosureDeclarations(static_cast<int>(made_for->second->GetId()));
              std::vector<SlotType> layout;
              DclrLayout(captures.second, captures.first, layout);
              if(index < 0 || receivers[i].end > (long)layout.size()) {
                return Fail(receivers[i].ip, L"a captured variable beyond the lambda's closure", true);
              }
              if(as_float != (layout[index].kind == RK_FLOAT) || (as_func && layout[index].part != 1)) {
                return Fail(receivers[i].ip, L"a captured variable stored as another kind than the lambda declares", true);
              }
            }
          }
          if(cls == NO_CLASS && origin[r] == ORIGIN_SELF && !method->IsLambda() && method->GetClass()) {
            cls = method->GetClass()->GetId();
          }
          StackClass* known = cls >= 0 ? prgm->GetClass(cls) : nullptr;
          if(known) {
            if(receivers[i].end * (long)sizeof(size_t) > known->GetInstanceMemorySize()) {
              return Fail(receivers[i].ip, L"an instance variable beyond the object's class", true);
            }
            const InstructionType access = instrs[receivers[i].ip].GetType();
            const bool as_float = access == LOAD_FLOAT_VAR || access == STOR_FLOAT_VAR || access == COPY_FLOAT_VAR;
            const bool as_func = access == LOAD_FUNC_VAR || access == STOR_FUNC_VAR;
            const long index = instrs[receivers[i].ip].GetOperand();
            const std::vector<SlotType>& layout = LayoutOf(cls);
            if(CLASS_RULES && index >= 0 && index < (long)layout.size()) {
              if(as_float != (layout[index].kind == RK_FLOAT) || (as_func && layout[index].part != 1)) {
                return Fail(receivers[i].ip, L"an instance variable read as another kind than its class declares", true);
              }
            }
          }
          else if(CLASS_RULES && context && cls != CLOSURE) {
            // any other reference whose static class is spelled: no field past the
            // largest object of that class's family
            ClassSet claimed;
            ClassesOf(receivers[i].value, claimed);
            if(claimed.n == 1 && !IsInterface(claimed.ids[0]) &&
               receivers[i].end * (INT64_VALUE)sizeof(size_t) > FamilySize(claimed.ids[0])) {
              return Fail(receivers[i].ip, L"an instance variable beyond every class the object can be", true);
            }
          }
        }
      }

      // a new object is the receiver only of its own class's methods, or of those
      // of a class it inherits from or an interface it implements
      for(size_t i = 0; i < calls.size(); ++i) {
        const Facts& f = facts[Find(calls[i].receiver)];
        if(f.cls_made >= 0 && f.made == F_REF && !IsA(f.cls_made, calls[i].callee_cls)) {
          return Fail(calls[i].ip, L"a method called on a new object of another class", true);
        }
        if(f.cls_made == CLOSURE && f.made == F_REF) {
          return Fail(calls[i].ip, L"a method called on a closure", true);
        }
      }
      if(!CheckClasses()) {
        return false;
      }

      // A local is typed by its declaration, so a load starts a new value: what
      // was stored is met with what the loads are used as here. An enum is an Int
      // in a reference-typed local, so a number may be stored; it may not then be
      // read through.
      if(!local_loads.empty() && !local_stores.empty()) {
        struct LocalFacts { uint8_t made = 0; uint8_t shape_made = 0; long number_ip = -1; };
        size_t slots = locals.size();
        for(size_t i = 0; i < local_stores.size(); ++i) {
          if(local_stores[i].id >= (long)slots) {
            slots = (size_t)local_stores[i].id + 1;
          }
        }
        std::vector<LocalFacts> stored(slots);
        for(size_t i = 0; i < local_stores.size(); ++i) {
          const LocalUse& st = local_stores[i];
          if(st.id < 0 || st.id >= (long)stored.size()) {
            continue;
          }
          const int v = Find(st.value);
          LocalFacts& lf = stored[st.id];
          lf.made |= facts[v].made;
          lf.shape_made |= facts[v].shape_made;
          if(lf.number_ip < 0 && (lit_ip[v] >= 0 || (facts[v].made & (F_INT | F_FLOAT | F_CLS)))) {
            lf.number_ip = st.ip;
          }
        }
        // one local copied to another carries what it holds: 'b := a' where a number
        // was stored to a. A value is a load of the locals listed for it.
        std::vector<std::pair<int, long> > loaded;
        loaded.reserve(local_loads.size());
        for(size_t i = 0; i < local_loads.size(); ++i) {
          if(local_loads[i].id >= 0 && local_loads[i].id < (long)stored.size()) {
            loaded.push_back(std::make_pair(Find(local_loads[i].value), local_loads[i].id));
          }
        }
        std::sort(loaded.begin(), loaded.end());
        for(bool changed = true; changed; ) {
          changed = false;
          for(size_t i = 0; i < local_stores.size(); ++i) {
            const LocalUse& st = local_stores[i];
            if(st.id < 0 || st.id >= (long)stored.size()) {
              continue;
            }
            LocalFacts& to = stored[st.id];
            const int v = Find(st.value);
            for(std::vector<std::pair<int, long> >::const_iterator from = std::lower_bound(loaded.begin(), loaded.end(), std::make_pair(v, -1L));
                from != loaded.end() && from->first == v; ++from) {
              const LocalFacts& source = stored[from->second];
              if(to.number_ip < 0 && source.number_ip >= 0) {
                to.number_ip = st.ip;
                changed = true;
              }
              if((to.shape_made | source.shape_made) != to.shape_made) {
                to.shape_made |= source.shape_made;
                changed = true;
              }
            }
          }
        }
        for(size_t i = 0; i < local_loads.size(); ++i) {
          const LocalUse& ld = local_loads[i];
          if(ld.id < 0 || ld.id >= (long)stored.size()) {
            continue;
          }
          const int v = Find(ld.value);
          const LocalFacts& lf = stored[ld.id];
          if(deref[v] && lf.number_ip >= 0) {
            return Fail(lf.number_ip, L"a number stored to a local that is used as a reference", true);
          }
          if((lf.shape_made & SHAPE_OBJECT) && (facts[v].shape_used & SHAPE_ARRAY)) {
            return Fail(facts[v].array_ip, L"an object used as an array", true);
          }
          if((lf.shape_made & SHAPE_ARRAY) && (facts[v].shape_used & SHAPE_OBJECT)) {
            return Fail(facts[v].object_ip, L"an array used as an object", true);
          }
        }
      }
      return true;
    }

    // Load-time verification: every use of two words as a function value against
    // the methods its id may name. An id's sources are the literals it has met on
    // the stack, a spelled function type (a parameter, a call's result), and, for
    // a load of a local, what was stored to that local anywhere in the method (a
    // function-typed variable has one declared type, so every store fits it).
    inline bool Builder::CheckFunctions() {
      if(func_uses.empty()) {
        return true;
      }
      std::sort(func_stores.begin(), func_stores.end(),
                [](const FuncStore& a, const FuncStore& b) { return a.slot < b.slot; });
      std::vector<int> pending;
      std::vector<long> slots_seen;
      for(size_t u = 0; u < func_uses.size(); ++u) {
        const FuncUse& use = func_uses[u];
        // the lambda a new closure is paired with, where the id is one literal
        StackMethod* paired = nullptr;
        bool one_literal = true;
        pending.assign(1, Find(use.id));
        slots_seen.clear();
        bool direct = true;
        for(size_t next = 0; next < pending.size(); ++next) {
          const int root = pending[next];
          int member = root;
          do {
            if(tag[member] == TAG_LIT && made_ip[member] >= 0 && instrs[made_ip[member]].GetInt64Operand() != 0) {
              // (the literal zero is the Nil function value: a call through it is
              // the runtime's Nil error)
              StackMethod* named = FunctionOf(instrs[made_ip[member]].GetInt64Operand());
              if(!named) {
                return Fail(made_ip[member], L"a function value that names no method", true);
              }
              if(use.words >= 0) {
                std::vector<SlotType> rtrn;
                ReturnWords(named->GetReturnName(), named->GetReturn(), rtrn);
                if(named->GetParamCount() != use.words || static_cast<int>(rtrn.size()) != use.rtrn_words ||
                   (rtrn.size() == 1 && (rtrn[0].kind == RK_FLOAT) != use.rtrn_float)) {
                  return Fail(use.ip, L"a function value used with other parameters or another return than its method's", true);
                }
              }
              if(direct) {
                if(paired && paired != named) {
                  one_literal = false;
                }
                paired = named;
              }
            }
            else if(spelled[member] && use.words >= 0) {
              long words;
              int rtrn_words;
              bool rtrn_float;
              if(ParseFunctionType(*spelled[member], words, rtrn_words, rtrn_float) &&
                 (words != use.words || rtrn_words != use.rtrn_words || (rtrn_words == 1 && rtrn_float != use.rtrn_float))) {
                return Fail(use.ip, L"a function value used with other parameters or another return than its type's", true);
              }
            }
            const long slot = func_load_slot[member];
            if(slot >= 0 && std::find(slots_seen.begin(), slots_seen.end(), slot) == slots_seen.end()) {
              slots_seen.push_back(slot);
              FuncStore key = { slot, 0 };
              for(std::vector<FuncStore>::const_iterator st = std::lower_bound(func_stores.begin(), func_stores.end(), key,
                    [](const FuncStore& a, const FuncStore& b) { return a.slot < b.slot; });
                  st != func_stores.end() && st->slot == slot; ++st) {
                const int stored = Find(st->id);
                if(std::find(pending.begin(), pending.end(), stored) == pending.end()) {
                  pending.push_back(stored);
                }
              }
            }
            member = ring[member];
          } while(member != root);
          direct = false;
        }

        // A lambda reads its captures through the closure it is called with, and the
        // collector walks that closure by the lambda's declarations: the closure is
        // one made large enough for them (or one this method was handed whole).
        if(paired && paired->IsLambda() && paired->GetClass()) {
          const std::pair<int, StackDclr**> captures = paired->GetClass()->GetClosureDeclarations(static_cast<int>(paired->GetId()));
          long capture_words = 0;
          for(int i = 0; i < captures.first; ++i) {
            capture_words += captures.second[i]->type == FUNC_PARM ? 2 : 1;
          }
          const int closure = Find(use.closure);
          if(capture_words > 0) {
            bool fits = false;
            int member = closure;
            do {
              switch(tag[member]) {
              case TAG_CLOSURE_NEW:
                fits = fits || instrs[made_ip[member]].GetOperand() >= capture_words * (long)sizeof(size_t);
                break;
              case TAG_FUNC_WORD:
              case TAG_NONE:
                fits = true;
                break;
              case TAG_SELF:
                fits = fits || method->IsLambda();
                break;
              default:
                break;
              }
              member = ring[member];
            } while(member != closure);
            if(!fits) {
              return Fail(use.ip, L"a lambda paired with a closure that was not made for its captures", true);
            }
          }
          if(facts[closure].cls_made == CLOSURE) {
            std::unordered_map<int, StackMethod*>::iterator known = closure_lambda.find(closure);
            if(known == closure_lambda.end()) {
              closure_lambda[closure] = one_literal ? paired : nullptr;
            }
            else if(known->second != paired || !one_literal) {
              known->second = nullptr;
            }
          }
        }
      }
      return true;
    }

    // Load-time verification: each reference whose class is known against the class
    // its use needs. A receiver's is settled once every method has been walked
    // (Loader::Verify): whether the callee reads its instance is the callee's fact.
    inline bool Builder::CheckClasses() {
      if(!CLASS_RULES) {
        return true;
      }
      for(size_t i = 0; i < class_reqs.size(); ++i) {
        const ClassReq& req = class_reqs[i];
        ClassSet known;
        ClassesOf(req.value, known);
        for(uint8_t k = 0; k < known.n; ++k) {
          bool fits = false;
          if(req.any_of) {
            // "A|B": the classes of the library methods the trap is written in
            const wchar_t* at = req.any_of;
            while(*at && !fits) {
              const wchar_t* end = at;
              while(*end && *end != L'|') {
                ++end;
              }
              StackClass* cls = prgm->GetClass(std::wstring(at, end));
              fits = cls && Related(known.ids[k], cls->GetId());
              at = *end ? end + 1 : end;
            }
          }
          else {
            fits = Related(known.ids[k], req.cls);
          }
          if(!fits) {
            switch(req.kind) {
            case REQ_STORE:
              return Fail(req.ip, L"an object stored to a parameter of another class", true);
            case REQ_ARGUMENT:
              return Fail(req.ip, L"an object passed where the parameter is of another class", true);
            case REQ_RETURN:
              return Fail(req.ip, L"an object returned where the method returns another class", true);
            default:
              return Fail(req.ip, L"a trap given an object of another class than it reads", true);
            }
          }
        }
      }

      if(context) {
        // The instance, and the loads of a local that holds nothing else. (The
        // inliner parks a function's caller's instance in a local to hand it on,
        // and may use that local again for an object it does read: a local with
        // any other store is not taken for the instance, which only means fewer
        // callers are held to the callee's class.)
        size_t slots = locals.size();
        for(size_t i = 0; i < local_stores.size(); ++i) {
          if(local_stores[i].id >= (long)slots) {
            slots = (size_t)local_stores[i].id + 1;
          }
        }
        std::vector<char> holds_self(slots, 0);
        std::vector<char> is_self(parent.size(), 0);
        for(size_t v = 0; v < parent.size(); ++v) {
          if(Find(static_cast<int>(v)) == static_cast<int>(v) && facts[v].from_self) {
            is_self[v] = 1;
          }
        }
        // the one local a value is a load of: -1 for none, -2 for more than one
        std::vector<long> loaded_from(parent.size(), -1);
        for(size_t i = 0; i < local_loads.size(); ++i) {
          const int v = Find(local_loads[i].value);
          const long id = local_loads[i].id;
          if(id < 0 || id >= (long)slots) {
            loaded_from[v] = -2;
          }
          else if(loaded_from[v] == -1) {
            loaded_from[v] = id;
          }
          else if(loaded_from[v] != id) {
            loaded_from[v] = -2;
          }
        }
        for(size_t i = 0; i < local_stores.size(); ++i) {
          if(local_stores[i].id >= 0) {
            holds_self[local_stores[i].id] = 1;
          }
        }
        for(bool changed = true; changed; ) {
          changed = false;
          // a local with a store of anything else no longer holds the instance
          for(size_t i = 0; i < local_stores.size(); ++i) {
            const LocalUse& st = local_stores[i];
            if(st.id >= 0 && holds_self[st.id]) {
              const int v = Find(st.value);
              const bool self = facts[v].from_self || (loaded_from[v] >= 0 && holds_self[loaded_from[v]]);
              if(!self) {
                holds_self[st.id] = 0;
                changed = true;
              }
            }
          }
        }
        for(size_t i = 0; i < local_loads.size(); ++i) {
          const LocalUse& ld = local_loads[i];
          if(ld.id >= 0 && ld.id < (long)slots && holds_self[ld.id]) {
            is_self[Find(ld.value)] = 1;
          }
        }
        VerifyContext::MethodFacts& own = context->methods[method];
        own.reads_self = frame_trap;
        for(size_t v = 0; v < parent.size() && !own.reads_self; ++v) {
          if(is_self[v] && facts[v].consumed) {
            own.reads_self = true;
          }
        }
        for(size_t i = 0; i < calls.size(); ++i) {
          StackMethod* callee = calls[i].callee;
          if(!callee || callee->IsVirtual() || !callee->GetClass()) {
            continue;
          }
          if(is_self[Find(calls[i].receiver)]) {
            own.hands_self_to.push_back(callee);
          }
          ClassSet known;
          ClassesOf(calls[i].receiver, known);
          for(uint8_t k = 0; k < known.n; ++k) {
            if(!Related(known.ids[k], callee->GetClass()->GetId())) {
              context->receiver_checks.push_back({ method, calls[i].ip, callee, known.ids[k] });
            }
          }
        }
      }
      return true;
    }

    inline void Builder::Run() {
      map->ok = true;
      map->height.assign(count, -1);
      map->pops.assign(count, -1);
      map->offset.assign(count, 0);
      map->andor.assign(count, RK_UNKNOWN);
      arena.clear();
      state_at.assign(count, 0);
      state_size.assign(count, 0);
      andor_state.assign(count, -1);
      seen.assign(count, false);
      if(verify) {
        // about one value an instruction: grow the per-value tables once
        const size_t values = static_cast<size_t>(count) + 16;
        parent.reserve(values);
        kind.reserve(values);
        use.reserve(values);
        elem.reserve(values);
        origin.reserve(values);
        deref.reserve(values);
        lit_ip.reserve(values);
        facts.reserve(values);
        class_parent.reserve(values);
        claims.reserve(values);
        tag.reserve(values);
        made_ip.reserve(values);
        ring.reserve(values);
        spelled.reserve(values);
        func_load_slot.reserve(values);
        edges.reserve(values + values / 4);
      }

      // locals by id: the and/or slot first when the method has one
      StackDclr** dclrs = method->GetDeclarations();
      const long num_dclrs = method->GetNumberDeclarations();
      if(method->HasAndOr()) {
        locals.push_back({ RK_UNKNOWN, RK_UNKNOWN });
        local_is_andor.push_back(true);
      }
      for(long i = 0; i < num_dclrs; ++i) {
        if(dclrs[i]->type == FUNC_PARM) {
          locals.push_back({ RK_INT, RK_UNKNOWN, SHAPE_NONE, 1 });
          locals.push_back({ RK_REF, RK_UNKNOWN, SHAPE_NONE, 2 });
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
        if(verify && method->IsLambda()) {
          const std::pair<int, StackDclr**> captures = cls->GetClosureDeclarations(static_cast<int>(method->GetId()));
          DclrLayout(captures.second, captures.first, closure_fields);
        }
      }

      if(count < 1) {
        Fail(0, L"a method with no instructions");
        return;
      }

      // An interpreted call leaves the arguments on the operand stack, and the
      // method's first instructions store them (the receiver went to mem[0])
      std::vector<int>& entry = entry_stack;
      entry.clear();
      std::vector<SlotType>& params = scratch_params;
      ParamWords(method, params);
      for(size_t i = 0; i < params.size(); ++i) {
        entry.push_back(NewValue(RK_UNKNOWN));
        // the callers were held to the declarations (MTHD_CALL)
        if(verify) {
          Made(entry.back(), params[i].kind, params[i].shape);
          MadeArrayKind(entry.back(), params[i].ary);
          if(params[i].part == 1) {
            facts[entry.back()].made = F_FUNC;
          }
          else if(params[i].part == 2) {
            tag[entry.back()] = TAG_FUNC_WORD;
          }
          if(context) {
            const VerifyContext::Signature& own = SignatureOf(method);
            if(i < own.param_class.size()) {
              Claim(entry.back(), own.param_class[i]);
            }
            if(params[i].part == 1 && i < own.param_function.size() && !own.param_function[i].empty()) {
              spelled[entry.back()] = &own.param_function[i];
            }
          }
        }
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
        if(!CheckFacts()) {
          return;
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
          if(seen[ip] && state_size[ip] > deepest) {
            deepest = state_size[ip];
          }
        }
        if(deepest > entry.size() + MAX_OPERAND_GROWTH) {
          Fail(0, L"the operand stack grows to " + std::to_wstring(deepest - entry.size()) +
                  L" words past the arguments, more than a call's room", true);
          return;
        }
      }

      if(verify) {
        return;
      }

      for(long ip = 0; ip < count; ++ip) {
        if(!seen[ip]) {
          continue;
        }
        map->height[ip] = static_cast<int>(state_size[ip]);
        map->offset[ip] = static_cast<uint32_t>(map->kinds.size());
        for(size_t i = 0; i < state_size[ip]; ++i) {
          map->kinds.push_back(Effective(arena[state_at[ip] + i]));
        }
        if(andor_state[ip] >= 0) {
          map->andor[ip] = Effective(andor_state[ip]);
        }
      }
    }
  }

  inline RefMap* RefMap::Build(StackProgram* prgm, StackMethod* method, bool verify, VerifyContext* context) {
    RefMap* map = new RefMap;
    ref_map_detail::Builder builder(prgm, method, map, verify, context);
    builder.Run();
    return map;
  }

  inline bool RefMap::Verify(StackProgram* prgm, StackMethod* method, VerifyContext* context, std::wstring& why, long& where) {
    if(!context->map) {
      context->map = new RefMap;
    }
    RefMap* map = context->map;
    map->ok = false;
    map->refuse = false;
    map->error.clear();
    map->error_ip = -1;
    map->kinds.clear();
    if(!context->builder) {
      context->builder = new ref_map_detail::Builder(prgm, method, map, true, context);
    }
    else {
      context->builder->Reset(method, map);
    }
    context->builder->Run();
    // only a failure that makes the method unsafe refuses the program; one that only
    // means the map cannot be built (an instruction it does not model) is not a verdict
    if(!map->ok && map->refuse) {
      why = map->error;
      where = map->error_ip;
      return true;
    }
    return false;
  }

  inline VerifyContext::~VerifyContext() {
    delete builder;
    delete map;
  }

#endif
