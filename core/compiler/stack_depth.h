/***************************************************************************
 * The operand-stack depth of a method, as the loader counts it.
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

#include "emit.h"
#include "linker.h"
#include "diagnostic.h"
#include "../shared/stack_limit.h"
#include "../shared/trap_signatures.h"
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace backend {
  //
  // WHY THIS IS HERE
  // ----------------
  // The VM gives a call a fixed room on the operand stack and the loader refuses
  // a method that can stand more than OPERAND_STACK_MAX_GROWTH words above its
  // arguments (vm/arch/ref_map.h). Nothing in the compiler counted those words,
  // so an expression of nested calls with many arguments compiled, and the
  // program was then refused whole when it was run: "the operand stack grows to
  // 1023 words", of a method, with no line. This counts them over the code obc
  // is about to write and reports the expression, in the source's terms.
  //
  // THE COUNT IS THE LOADER'S
  // -------------------------
  // The worst this check can do is refuse a program the loader accepts, so it
  // does not estimate. It walks every path of the finished method (after the
  // optimizer and the inliner: the instructions as they are written) and keeps
  // the height on entry to each instruction it reaches, exactly as RefMap's
  // Builder does when it verifies: the same words popped and pushed for each
  // instruction (Step below mirrors Builder::Step case for case), a callee's
  // parameter and return words, a trap's from the same table
  // (shared/trap_signatures.h), a jump's target, a "select" table's targets and a
  // handler entered at its TRY_START's height. The number held to the limit is
  // the deepest of those entry heights above the arguments, which is the
  // loader's, and the limit is the loader's constant (shared/stack_limit.h).
  //
  // Where the walk meets anything the loader would not accept, or that has no
  // case here (a pop of an empty stack, two heights at a join, an unknown
  // callee, an instruction it does not model) it gives NO verdict: the method is
  // written as before and the loader decides. It never refuses on a guess.
  //
  // That the two counts agree is measured, not assumed: OBJECK_STACK_DEPTH_OUT
  // makes obc and obr each append a line for every method with its depth, and
  // programs/regression/check_stack_depth.py holds the two files of a program
  // equal.
  //
  // (Every instruction is written instructions::NAME here: the bootstrap build of
  // the compiler, -D_SYSTEM, has scanner tokens of the same names in scope.)
  //
  // Cost: one pass over a method's instructions, each visited once (a join only
  // compares a height), so the check is linear in the code written.
  //
  class StackDepth {
   public:
    struct Depth {
      bool known;       // false: no verdict (see above)
      long growth;      // deepest entry height above the arguments
      long at;          // the instruction that left the stack that deep (-1: the entry)
    };

   private:
    IntermediateProgram* program;
    Linker* linker;
    // jumps hold the index of the instruction after their label (an executable,
    // after JumpToLocation) or the label's id (a library)
    bool resolved;
    // a library's own classes and methods, by name, for LIB_MTHD_CALL
    bool names_built;
    std::unordered_map<std::wstring, IntermediateClass*> class_names;
    std::unordered_map<IntermediateClass*, std::unordered_map<std::wstring, IntermediateMethod*> > method_names;
    // scratch, kept between methods
    std::vector<long> heights;
    std::vector<long> worklist;
    std::unordered_map<long, long> labels;

    // an encoded return type as stack words (RefMap's ReturnWords); -1: no verdict
    static long ReturnWords(const std::wstring& name) {
      if(name.empty()) {
        return -1;
      }
      if(name[0] == L'n') {
        return 0;
      }
      return name[0] == L'm' ? 2 : 1;
    }

    // DYN_MTHD_CALL's return, named by a type code
    static long ReturnWords(long type) {
      switch(type) {
      case instructions::NIL_TYPE:
        return 0;
      case instructions::FUNC_TYPE:
        return 2;
      default:
        return 1;
      }
    }

    void BuildNames() {
      names_built = true;
      std::vector<IntermediateClass*> classes = program->GetClasses();
      for(size_t i = 0; i < classes.size(); ++i) {
        class_names[classes[i]->GetName()] = classes[i];
      }
    }

    // the words a call pops beside its receiver, and the words it pushes
    bool Callee(IntermediateInstruction* instr, long& params, long& rtrn) {
      if(instr->GetType() == instructions::MTHD_CALL) {
        IntermediateClass* klass = program->GetClass(static_cast<int>(instr->GetOperand()));
        IntermediateMethod* method = klass ? klass->GetMethod(static_cast<int>(instr->GetOperand2())) : nullptr;
        if(!method) {
          return false;
        }
        params = method->GetNumParams();
        rtrn = ReturnWords(method->GetReturnName());
        return params >= 0 && rtrn >= 0;
      }
      // LIB_MTHD_CALL: the class and the method by name, in this library or a linked one
      if(!names_built) {
        BuildNames();
      }
      std::unordered_map<std::wstring, IntermediateClass*>::iterator found = class_names.find(instr->GetOperand5());
      if(found != class_names.end()) {
        std::unordered_map<std::wstring, IntermediateMethod*>& methods = method_names[found->second];
        if(methods.empty()) {
          std::vector<IntermediateMethod*> all = found->second->GetMethods();
          for(size_t i = 0; i < all.size(); ++i) {
            methods[all[i]->GetName()] = all[i];
          }
        }
        std::unordered_map<std::wstring, IntermediateMethod*>::iterator method = methods.find(instr->GetOperand6());
        if(method == methods.end()) {
          return false;
        }
        params = method->second->GetNumParams();
        rtrn = ReturnWords(method->second->GetReturnName());
        return params >= 0 && rtrn >= 0;
      }
      LibraryClass* lib_klass = linker ? linker->SearchClassLibraries(instr->GetOperand5()) : nullptr;
      LibraryMethod* lib_method = lib_klass ? lib_klass->GetMethod(instr->GetOperand6()) : nullptr;
      if(!lib_method) {
        return false;
      }
      params = lib_method->GetNumParams();
      rtrn = ReturnWords(lib_method->GetEncodedReturn());
      return params >= 0 && rtrn >= 0;
    }

    // What the instruction at 'ip' pops and pushes, as Builder::Step has it when
    // it verifies. false: no verdict. 'falls' is whether control goes on to the
    // next instruction; a jump's targets are Measure's business.
    bool Step(const std::vector<IntermediateInstruction*>& instrs, size_t ip, long& pops, long& pushes, bool& falls) {
      IntermediateInstruction* instr = instrs[ip];
      // (an operand is read only where the instruction has it: the others are not set)
      falls = true;
      pops = pushes = 0;

      switch(instr->GetType()) {
      case instructions::LOAD_INT_LIT:
      case instructions::LOAD_CHAR_LIT:
      case instructions::LOAD_FLOAT_LIT:
      case instructions::NAN_INT: case instructions::INF_INT: case instructions::NEG_INF_INT:
      case instructions::NAN_FLOAT: case instructions::INF_FLOAT: case instructions::NEG_INF_FLOAT:
      case instructions::RAND_FLOAT:
      case instructions::LOAD_CLS_MEM:
      case instructions::LOAD_INST_MEM:
      case instructions::NEW_OBJ_INST:
      case instructions::NEW_FUNC_INST:
      // a library's forms, which the linker resolves to NEW_OBJ_INST and to a literal
      case instructions::LIB_NEW_OBJ_INST:
      case instructions::LIB_FUNC_DEF:
        pushes = 1;
        break;

      // A variable that is not a local's is read from or stored to the instance or
      // the class memory, which is popped first (the loader makes these
      // LOAD_LOCL_INT_VAR or LOAD_CLS_INST_INT_VAR, and so on)
      case instructions::LOAD_INT_VAR:
      case instructions::LOAD_FLOAT_VAR:
        pops = instr->GetOperand2() == instructions::LOCL ? 0 : 1;
        pushes = 1;
        break;

      case instructions::STOR_INT_VAR:
      case instructions::STOR_FLOAT_VAR:
        pops = instr->GetOperand2() == instructions::LOCL ? 1 : 2;
        break;

      // stores the word on top and leaves it
      case instructions::COPY_INT_VAR:
      case instructions::COPY_FLOAT_VAR:
        pops = instr->GetOperand2() == instructions::LOCL ? 1 : 2;
        pushes = 1;
        break;

      case instructions::LOAD_FUNC_VAR:
        pops = instr->GetOperand2() == instructions::LOCL ? 0 : 1;
        pushes = 2;
        break;

      case instructions::STOR_FUNC_VAR:
        pops = instr->GetOperand2() == instructions::LOCL ? 2 : 3;
        break;

      // the array, then an index for each dimension
      case instructions::LOAD_BYTE_ARY_ELM:
      case instructions::LOAD_CHAR_ARY_ELM:
      case instructions::LOAD_INT_ARY_ELM:
      case instructions::LOAD_FLOAT_ARY_ELM:
        if(instr->GetOperand() < 0) {
          return false;
        }
        pops = 1 + instr->GetOperand();
        pushes = 1;
        break;

      case instructions::STOR_BYTE_ARY_ELM:
      case instructions::STOR_CHAR_ARY_ELM:
      case instructions::STOR_INT_ARY_ELM:
      case instructions::STOR_FLOAT_ARY_ELM:
        if(instr->GetOperand() < 0) {
          return false;
        }
        pops = 2 + instr->GetOperand();
        break;

      case instructions::EQL_INT: case instructions::NEQL_INT: case instructions::LES_INT: case instructions::GTR_INT: case instructions::LES_EQL_INT: case instructions::GTR_EQL_INT:
      case instructions::EQL_FLOAT: case instructions::NEQL_FLOAT: case instructions::LES_FLOAT: case instructions::GTR_FLOAT: case instructions::LES_EQL_FLOAT: case instructions::GTR_EQL_FLOAT:
      case instructions::AND_INT: case instructions::OR_INT: case instructions::ADD_INT: case instructions::SUB_INT: case instructions::MUL_INT: case instructions::DIV_INT: case instructions::MOD_INT:
      case instructions::BIT_AND_INT: case instructions::BIT_OR_INT: case instructions::BIT_XOR_INT: case instructions::SHL_INT: case instructions::SHR_INT:
      case instructions::ADD_FLOAT: case instructions::SUB_FLOAT: case instructions::MUL_FLOAT: case instructions::DIV_FLOAT:
      case instructions::ATAN2_FLOAT: case instructions::MOD_FLOAT: case instructions::POW_FLOAT:
      case instructions::S2I:
        pops = 2;
        pushes = 1;
        break;

      case instructions::LOAD_ARY_SIZE:
      case instructions::BIT_NOT_INT:
      case instructions::FLOR_FLOAT: case instructions::CEIL_FLOAT: case instructions::TRUNC_FLOAT: case instructions::SIN_FLOAT: case instructions::COS_FLOAT: case instructions::TAN_FLOAT:
      case instructions::ASIN_FLOAT: case instructions::ACOS_FLOAT: case instructions::ATAN_FLOAT: case instructions::LOG2_FLOAT: case instructions::CBRT_FLOAT: case instructions::COSH_FLOAT:
      case instructions::SINH_FLOAT: case instructions::TANH_FLOAT: case instructions::ACOSH_FLOAT: case instructions::ASINH_FLOAT: case instructions::ATANH_FLOAT: case instructions::LOG_FLOAT:
      case instructions::ROUND_FLOAT: case instructions::EXP_FLOAT: case instructions::LOG10_FLOAT: case instructions::SQRT_FLOAT: case instructions::GAMMA_FLOAT:
      case instructions::I2F:
      case instructions::F2I:
      case instructions::S2F:
      case instructions::OBJ_INST_CAST:
      case instructions::OBJ_TYPE_OF:
      case instructions::LIB_OBJ_INST_CAST:
      case instructions::LIB_OBJ_TYPE_OF:
        pops = 1;
        pushes = 1;
        break;

      case instructions::I2S:
        pops = 3;
        break;

      case instructions::F2S:
        pops = 2;
        break;

      case instructions::MTHD_CALL:
      case instructions::LIB_MTHD_CALL: {
        long params;
        long rtrn;
        if(!Callee(instr, params, rtrn)) {
          return false;
        }
        // the receiver on top, then the arguments
        pops = 1 + params;
        pushes = rtrn;
      }
        break;

      case instructions::DYN_MTHD_CALL:
        if(instr->GetOperand() < 0) {
          return false;
        }
        // the function's id, its closure, then the arguments
        pops = 2 + instr->GetOperand();
        pushes = ReturnWords(instr->GetOperand2());
        break;

      case instructions::JMP:
        pops = instr->GetOperand2() >= 0 ? 1 : 0;
        falls = instr->GetOperand2() >= 0;
        break;

      case instructions::LBL:
      case instructions::TRY_START:
      case instructions::TRY_END:
      case instructions::END_STMTS:
      case instructions::ASYNC_MTHD_CALL:
      case instructions::THREAD_JOIN:
      case instructions::THREAD_MUTEX:
      case instructions::EXT_LIB_LOAD:
      case instructions::EXT_LIB_UNLOAD:
      case instructions::EXT_LIB_FUNC_CALL:
        break;

      case instructions::RTRN:
        falls = false;
        break;

      case instructions::NEW_BYTE_ARY:
      case instructions::NEW_CHAR_ARY:
      case instructions::NEW_INT_ARY:
      case instructions::NEW_OBJ_ARY:
      case instructions::NEW_FLOAT_ARY:
        if(instr->GetOperand() < 0) {
          return false;
        }
        pops = instr->GetOperand();
        pushes = 1;
        break;

      // length, source offset, source, destination offset, destination
      case instructions::CPY_BYTE_ARY: case instructions::CPY_CHAR_ARY: case instructions::CPY_INT_ARY: case instructions::CPY_FLOAT_ARY:
        pops = 5;
        pushes = 1;
        break;

      case instructions::ZERO_BYTE_ARY: case instructions::ZERO_CHAR_ARY: case instructions::ZERO_INT_ARY: case instructions::ZERO_FLOAT_ARY:
      case instructions::POP_INT:
      case instructions::POP_FLOAT:
      case instructions::THREAD_SLEEP:
      case instructions::CRITICAL_START:
      case instructions::CRITICAL_END:
        pops = 1;
        break;

      // A trap's handler pops what the instruction counts, its id among them (the
      // literal just below), and leaves a word or not as its row says
      // (Builder::StepTrap). One the table does not hold, or that is written the
      // other way round, the loader refuses: no verdict here.
      case instructions::TRAP:
      case instructions::TRAP_RTRN: {
        if(ip < 1 || instrs[ip - 1]->GetType() != instructions::LOAD_INT_LIT) {
          return false;
        }
        const trap_signatures::Row* row = trap_signatures::Find(instrs[ip - 1]->GetOperand7());
        if(!row || row->rtrn != (instr->GetType() == instructions::TRAP_RTRN) || (row->count >= 0 && row->count != instr->GetOperand()) || instr->GetOperand() < 1) {
          return false;
        }
        pops = instr->GetOperand();
        // (a literal array of strings hands its array back)
        pushes = (row->count < 0 || row->pushes) ? 1 : 0;
      }
        break;

      case instructions::SWAP_INT:
        pops = 2;
        pushes = 2;
        break;

      // the value selected on; its targets are Measure's business
      case instructions::JMP_TABLE:
        pops = 1;
        falls = false;
        break;

      default:
        return false;
      }

      return pushes >= 0;
    }

    // where a jump operand lands: the index of the first instruction run there
    bool Target(long operand, size_t count, size_t& to) {
      if(resolved) {
        if(operand < 0 || static_cast<size_t>(operand) >= count) {
          return false;
        }
        to = static_cast<size_t>(operand);
        return true;
      }
      std::unordered_map<long, long>::iterator found = labels.find(operand);
      if(found == labels.end() || found->second < 0) {
        return false;
      }
      to = static_cast<size_t>(found->second);
      return to < count;
    }

    // control reaches 'to' at 'height' (Builder::Flow): two heights at one
    // instruction is a method the loader refuses, so no verdict
    bool Enter(size_t from, size_t to, long height, long& deepest, long& deepest_from) {
      if(heights[to] < 0) {
        heights[to] = height;
        worklist.push_back(static_cast<long>(to));
        if(height > deepest) {
          deepest = height;
          deepest_from = static_cast<long>(from);
        }
        return true;
      }
      return heights[to] == height;
    }

    static std::string DumpPath() {
#ifdef _WIN32
      char* value = nullptr;
      size_t length = 0;
      std::string path;
      if(_dupenv_s(&value, &length, "OBJECK_STACK_DEPTH_OUT") == 0 && value) {
        path = value;
      }
      free(value);
      return path;
#else
      const char* value = getenv("OBJECK_STACK_DEPTH_OUT");
      return value ? std::string(value) : std::string();
#endif
    }

    // The source place of the instruction that left the stack deepest: its own
    // expression or statement, or the nearest one before it that has either (a
    // jump the optimizer rewrote, or code inlined from a library, has none).
    static bool Place(const std::vector<IntermediateInstruction*>& instrs, long at, std::wstring& file, int& line, int& pos) {
      for(long i = at; i >= 0; --i) {
        frontend::ParseNode* node = instrs[i]->GetExpression();
        if(!node) {
          node = instrs[i]->GetStatement();
        }
        if(node && !node->GetFileName().empty() && node->GetLineNumber() > 0) {
          file = node->GetFileName();
          line = node->GetLineNumber();
          pos = node->GetLinePosition() > 0 ? node->GetLinePosition() : 1;
          return true;
        }
      }
      return false;
    }

   public:
    // 'resolved': the methods' jumps were resolved to instruction indexes
    // (ItermediateOptimizer::JumpToLocation, which an executable's are and a
    // library's are not). The linker is only read for a library's calls.
    StackDepth(IntermediateProgram* p, Linker* l, bool r) : program(p), linker(l), resolved(r), names_built(false) {
    }

    Depth Measure(IntermediateMethod* method, const std::vector<IntermediateInstruction*>& instrs) {
      const Depth none = { false, -1, -1 };
      const size_t count = instrs.size();
      const long entry = method->GetNumParams();
      if(count < 1 || entry < 0) {
        return none;
      }

      if(!resolved) {
        // a label's place is the instruction after it; one defined twice names none
        labels.clear();
        for(size_t i = 0; i < count; ++i) {
          if(instrs[i]->GetType() == instructions::LBL) {
            std::pair<std::unordered_map<long, long>::iterator, bool> added =
              labels.insert(std::pair<long, long>(instrs[i]->GetOperand(), static_cast<long>(i + 1)));
            if(!added.second) {
              added.first->second = -1;
            }
          }
        }
      }

      heights.assign(count, -1);
      worklist.clear();
      heights[0] = entry;
      worklist.push_back(0);
      long deepest = entry;
      long deepest_from = -1;

      while(!worklist.empty()) {
        const size_t ip = static_cast<size_t>(worklist.back());
        worklist.pop_back();

        long pops;
        long pushes;
        bool falls;
        if(!Step(instrs, ip, pops, pushes, falls)) {
          return none;
        }
        long height = heights[ip];
        // (never below the method's own arguments either way: the loader's stack
        // starts with them, and a pop of them is not what the compiler writes)
        if(height < pops) {
          return none;
        }
        height += pushes - pops;

        // every place control goes from here, each entered at 'height'
        IntermediateInstruction* instr = instrs[ip];
        switch(instr->GetType()) {
        case instructions::JMP:
        case instructions::TRY_START: {
          // (a handler resumes with the stack as it was at its TRY_START)
          size_t to;
          if(!Target(instr->GetOperand(), count, to) || !Enter(ip, to, height, deepest, deepest_from)) {
            return none;
          }
        }
          break;

        case instructions::JMP_TABLE: {
          const long range = instr->GetOperand2();
          if(range < 0 || ip + static_cast<size_t>(range) >= count) {
            return none;
          }
          size_t to;
          for(long i = 1; i <= range; ++i) {
            if(instrs[ip + i]->GetType() != instructions::JMP_TABLE_SLOT || !Target(instrs[ip + i]->GetOperand(), count, to) ||
               !Enter(ip, to, height, deepest, deepest_from)) {
              return none;
            }
          }
          if(!Target(instr->GetOperand3(), count, to) || !Enter(ip, to, height, deepest, deepest_from)) {
            return none;
          }
        }
          break;

        default:
          break;
        }
        if(falls) {
          // (off the end of the method: the loader refuses it)
          if(ip + 1 >= count || !Enter(ip, ip + 1, height, deepest, deepest_from)) {
            return none;
          }
        }
      }

      const Depth depth = { true, deepest - entry, deepest_from };
      return depth;
    }

    //
    // ONE COMPILATION, UP TO THREE ATTEMPTS
    // -------------------------------------
    // A String literal of a few elements is copied from the operand stack, a word
    // for each element and two more on top of whatever waits there
    // (STRING_LITERAL_STACK_MAX, context.h); built an element at a time it adds
    // three. The first is the smaller and faster code, and it is what took a
    // method with some 996 words of arguments waiting past the limit: a program
    // that fits the stack and that the loader refused. Which form a literal needs
    // is not known until the method is finished and counted, so the driver
    // compiles, counts, and where a method is over compiles again with the
    // literals of the methods that were over in the element form, and, if one
    // still is (a literal in a lambda is analyzed under its enclosing method),
    // once more with every literal in that form. Only what is still over then is
    // reported: an expression that does not fit however its literals are built.
    // A program no method of which is over the limit is compiled once, as before,
    // to the same bytes.
    //
    struct Attempts {
      int attempt;                       // 0: as written; 1: the methods below; 2: every method
      std::set<std::wstring> methods;    // encoded names of the methods found over the limit

      Attempts() : attempt(0) {
      }

      // before the analysis of an attempt
      void Apply(frontend::ParsedProgram* parsed) const {
        if(attempt > 0) {
          parsed->SetElementwiseStringLiterals(methods, attempt > 1);
        }
      }

      // after a count that found 'over': true when the compilation is to be made again
      bool Again(const std::set<std::wstring>& over) {
        if(attempt > 1) {
          return false;
        }
        methods.insert(over.begin(), over.end());
        ++attempt;
        return true;
      }
    };

    // Measures every method of the program. A method that needs more than the
    // loader allows is an error at the expression that needs it, and the program
    // is not written when there is one: 'over' names those methods. true when
    // there is none.
    //
    // OBJECK_STACK_DEPTH_OUT=<file> appends "<method>\t<depth>" for every method
    // ('?' for no verdict) of a program that is written: the lines obr writes
    // under the same variable when it loads it.
    static bool Check(IntermediateProgram* program, Linker* linker, bool is_lib, std::vector<frontend::Diagnostic>& errors,
                      std::set<std::wstring>& over) {
      const long limit = static_cast<long>(instructions::OPERAND_STACK_MAX_GROWTH);
      const std::string dump_path = DumpPath();
      const bool dumps = !dump_path.empty();
      std::ostringstream dump;

      StackDepth walker(program, linker, !is_lib);
      std::vector<IntermediateInstruction*> instrs;
      std::vector<IntermediateClass*> classes = program->GetClasses();
      for(size_t i = 0; i < classes.size(); ++i) {
        std::vector<IntermediateMethod*> methods = classes[i]->GetMethods();
        for(size_t j = 0; j < methods.size(); ++j) {
          IntermediateMethod* method = methods[j];
          std::vector<IntermediateBlock*> blocks = method->GetBlocks();
          if(blocks.size() == 1) {
            instrs = blocks[0]->GetInstructions();
          }
          else {
            instrs.clear();
            for(size_t k = 0; k < blocks.size(); ++k) {
              std::vector<IntermediateInstruction*> block_instrs = blocks[k]->GetInstructions();
              instrs.insert(instrs.end(), block_instrs.begin(), block_instrs.end());
            }
          }
          // (a declaration with no body is never entered, and the loader skips it)
          if(instrs.empty()) {
            continue;
          }

          const Depth depth = walker.Measure(method, instrs);
          if(dumps) {
            dump << UnicodeToBytes(method->GetName()) << '\t';
            if(depth.known) {
              dump << depth.growth;
            }
            else {
              dump << '?';
            }
            dump << '\n';
          }
          if(!depth.known || depth.growth <= limit) {
            continue;
          }

          over.insert(method->GetName());
          const std::wstring needs = L"needs an operand stack of " + std::to_wstring(depth.growth) +
            L" words, more than the limit of " + std::to_wstring(limit);
          std::wstring file;
          int line = 1;
          int pos = 1;
          if(Place(instrs, depth.at, file, line, pos)) {
            errors.push_back(frontend::Diagnostic(frontend::DIAG_ERROR, file, line, pos, L"This expression " + needs +
              L": split it into statements or use fewer arguments"));
          }
          else {
            // code with no source here: a method of a linked library, as it was compiled
            errors.push_back(frontend::Diagnostic(frontend::DIAG_ERROR, classes[i]->GetFileName(), 1, 1,
              L"The method '" + method->GetName() + L"' " + needs +
              L": split its expressions into statements or use fewer arguments"));
          }
        }
      }

      if(dumps && errors.empty()) {
        std::ofstream out(dump_path.c_str(), std::ios::app | std::ios::binary);
        out << dump.str();
      }

      return errors.empty();
    }
  };
}
