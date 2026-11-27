/***************************************************************************
 * Platform independent language optimizer.
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
 * - Neither the name of the Objeck team nor the names of its
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

#include "optimization.h"
#include "cfg.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>

namespace {
  // OBJECK_CFG_VERIFY=1 makes the optimizer build the control-flow graph of every method it
  // sees, before and after its passes, and check the graph's invariants (see Cfg::Verify). It
  // is how the library sources and the test programs are checked: compile them with it set and
  // read the summary on stderr.
  struct CfgVerifyStats {
    size_t methods = 0;
    size_t blocks = 0;
    size_t loops = 0;
    size_t irreducible = 0;
    size_t not_well_formed = 0;
    size_t failures = 0;
    bool enabled = false;

    ~CfgVerifyStats() {
      if(enabled) {
        std::wcerr << L"cfg-verify: methods=" << methods << " blocks=" << blocks << " loops=" << loops
                  << " irreducible_edges=" << irreducible << " not_well_formed=" << not_well_formed
                  << " failures=" << failures << std::endl;
      }
    }
  };

  CfgVerifyStats cfg_verify_stats;

  // OBJECK_CFG_DUMP=<file> appends every method's final graph (the one JumpToLocation is
  // about to resolve) to the file: one 'M' line per method, one 'B' line per block with its
  // instruction range, reachability, normal successors and handler successors. The dynamic
  // oracle (tools/cicd/cfg_oracle.py) checks the VM's executed transitions against it.
  void DumpMethodCfg(IntermediateMethod* method, const std::vector<IntermediateBlock*>& blocks)
  {
    static const std::string path = Cfg::DumpPath();
    if(path.empty()) {
      return;
    }
    std::ofstream out(path.c_str(), std::ios::app);
    const std::wstring wname = method->GetName();
    const std::string name(wname.begin(), wname.end());
    if(blocks.size() != 1) {
      out << "X " << name << " blocks=" << blocks.size() << std::endl;
      return;
    }
    const std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
    Cfg cfg(instrs);
    if(!cfg.WellFormed()) {
      out << "X " << name << " not-well-formed: " << cfg.Problem() << std::endl;
      return;
    }
    out << "M " << name << " " << instrs.size() << " " << cfg.Size() << std::endl;
    for(const Cfg::Block& b : cfg.Blocks()) {
      size_t last = b.last;
      while(last > b.first && instrs[last]->GetType() == JMP_TABLE_SLOT) {
        --last;
      }
      out << "B " << b.id << " " << b.first << " " << b.last << " " << last << " " << (b.reachable ? 1 : 0) << " " << (instrs[b.first]->GetType() == LBL ? 1 : 0) << " n";
      for(int s : b.normal) {
        out << " " << s;
      }
      out << " h";
      for(int s : b.handlers) {
        out << " " << s;
      }
      out << std::endl;
    }
  }

  void VerifyMethodCfg(IntermediateMethod* method, const std::vector<IntermediateBlock*>& blocks, const char* stage)
  {
    if(!cfg_verify_stats.enabled) {
      return;
    }
    for(IntermediateBlock* block : blocks) {
      const std::vector<IntermediateInstruction*> instrs = block->GetInstructions();
      Cfg cfg(instrs);
      ++cfg_verify_stats.methods;
      if(!cfg.WellFormed()) {
        ++cfg_verify_stats.not_well_formed;
        std::wcerr << L"cfg-verify: not well formed (" << stage << L") " << method->GetName().c_str() << L": ";
        std::wcerr << std::wstring(cfg.Problem().begin(), cfg.Problem().end()) << std::endl;
        continue;
      }
      cfg_verify_stats.blocks += cfg.Size();
      cfg_verify_stats.loops += cfg.Loops().size();
      cfg_verify_stats.irreducible += cfg.IrreducibleEdges();
      cfg.ComputeLiveness();
      std::string why;
      if(!cfg.Verify(&why)) {
        ++cfg_verify_stats.failures;
        std::wcerr << L"cfg-verify: FAIL (" << stage << L") " << method->GetName().c_str() << L": ";
        std::wcerr << std::wstring(why.begin(), why.end()) << std::endl;
      }
    }
  }
}
#include <tuple>

using namespace backend;

ItermediateOptimizer::ItermediateOptimizer(IntermediateProgram* p, int u, std::wstring o, bool l, bool d)
{
  program = p;
  cur_line_num = -1;
  merge_blocks = false;
  unconditional_label = u;
  is_lib = l;

  if(d) {
    optimization_level = 0;
  }
  else {
    if(o == L"s0") {
      optimization_level = 0;
    }
    else if(o == L"s1") {
      optimization_level = 1;
    }
    else if(o == L"s2") {
      optimization_level = 2;
    }
    else {
      // s3, and anything that is not s0..s2
      optimization_level = 3;
    }
  }

  jump_inline_offset = 0;
  cfg_verify_stats.enabled = Cfg::VerifyRequested();
}

void ItermediateOptimizer::Optimize()
{
#ifdef _DEBUG
  GetLogger() << L"\n--------- Optimizing Code ---------" << std::endl;
#endif

  // Pass 1 runs over EVERY method before pass 2 inlines anything. Interleaving them
  // per class meant a callee in a later class was inlined before its own cleanup,
  // dead-code, constant-propagation and tail-call work had run, while a callee in an
  // earlier class was inlined already optimized: the emitted bytecode depended on
  // class / link order. Inlining from fully optimized callees makes it order independent.
  std::vector<IntermediateClass*> klasses = program->GetClasses();
  for(size_t i = 0; i < klasses.size(); ++i) {
    // methods...
    std::vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
    for(size_t j = 0; j < methods.size(); ++j) {
      current_method = methods[j];
#ifdef _DEBUG
      GetLogger() << L"Optimizing method, pass 1: name='" << current_method->GetName() << "'" << std::endl;
#endif
      VerifyMethodCfg(current_method, current_method->GetBlocks(), "emitted");
      current_method->SetBlocks(OptimizeMethod(current_method->GetBlocks()));
      VerifyMethodCfg(current_method, current_method->GetBlocks(), "optimized");
    }
  }

  if(!is_lib) {
    for(size_t i = 0; i < klasses.size(); ++i) {
      std::vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
      for(size_t j = 0; j < methods.size(); ++j) {
        current_method = methods[j];
#ifdef _DEBUG
        GetLogger() << L"Optimizing method, pass 2: name='" << current_method->GetName() << "'" << std::endl;
#endif
        current_method->SetBlocks(InlineMethod(current_method->GetBlocks()));
      }
    }
  }
  
  if(optimization_level > 1) {
    klasses = program->GetClasses();
    for(size_t i = 0; i < klasses.size(); ++i) {
      std::vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
      for(size_t j = 0; j < methods.size(); ++j) {
        current_method = methods[j];
#ifdef _DEBUG
        GetLogger() << L"Optimizing labels, pass 3: name='" << current_method->GetName() << "'" << std::endl;
#endif
        current_method->SetBlocks(CleanLabelsLocation(current_method->GetBlocks()));

      }
    }
  }

  if(!is_lib) {
    klasses = program->GetClasses();
    for(size_t i = 0; i < klasses.size(); ++i) {
      std::vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
      for(size_t j = 0; j < methods.size(); ++j) {
        current_method = methods[j];
#ifdef _DEBUG
        GetLogger() << L"Optimizing jumps, pass 4: name='" << current_method->GetName() << "'" << std::endl;
#endif
        DumpMethodCfg(current_method, current_method->GetBlocks());
        current_method->SetBlocks(JumpToLocation(current_method->GetBlocks()));

      }
    }
  }
}

std::vector<IntermediateBlock*> ItermediateOptimizer::InlineMethod(std::vector<IntermediateBlock*> inputs)
{
  if(optimization_level > 2) {
    // inline methods
#ifdef _DEBUG
    GetLogger() << L"  Method inlining..." << std::endl;
#endif
    std::vector<IntermediateBlock*> outputs;
    while(!inputs.empty()) {
      IntermediateBlock* tmp = inputs.front();
      outputs.push_back(InlineMethod(tmp));
      // delete old block
      inputs.erase(inputs.begin());
      delete tmp;
      tmp = nullptr;
    }

    return outputs;
  }
  else {
    return inputs;
  }
}

std::vector<IntermediateBlock*> ItermediateOptimizer::CleanLabelsLocation(std::vector<IntermediateBlock*> inputs)
{
#ifdef _DEBUG
  GetLogger() << L"  Clean up labels..." << std::endl;
#endif
  // A merged label takes the next id from the counter, so the counter must first
  // pass every label this method already holds. A library method's labels were
  // numbered by the library's own compile, from the same base as this program's
  // counter, so they overlap it: a merged label could take the id of another
  // label in the same method, and its jumps then landed there. Adding three
  // classes to query.obl moved the numbering onto such a coincidence, and a
  // query in Plan:Run looped forever in any program built at -opt s2 or s3.
  unconditional_label = MaxLabel(inputs, unconditional_label);

  std::vector<IntermediateBlock*> outputs;
  while(!inputs.empty()) {
    IntermediateBlock* tmp = inputs.front();
    outputs.push_back(CleanLabelsLocation(tmp));
    // delete old block
    inputs.erase(inputs.begin());
    delete tmp;
    tmp = nullptr;
  }

  return outputs;
}

// the largest label id a method's blocks define or jump to, or 'floor' if larger
int ItermediateOptimizer::MaxLabel(std::vector<IntermediateBlock*>& inputs, int floor)
{
  int max_label = floor;
  for(auto* block : inputs) {
    for(auto* instr : block->GetInstructions()) {
      switch(instr->GetType()) {
      case LBL:
      case JMP:
      case JMP_TABLE_SLOT:
      case TRY_START:
        if(static_cast<int>(instr->GetOperand()) > max_label) {
          max_label = static_cast<int>(instr->GetOperand());
        }
        break;

      case JMP_TABLE:
        if(static_cast<int>(instr->GetOperand3()) > max_label) {
          max_label = static_cast<int>(instr->GetOperand3());
        }
        break;

      default:
        break;
      }
    }
  }

  return max_label;
}

std::vector<IntermediateBlock*> ItermediateOptimizer::JumpToLocation(std::vector<IntermediateBlock*> inputs)
{
#ifdef _DEBUG
  GetLogger() << L"  Updating jump locations..." << std::endl;
#endif
  std::vector<IntermediateBlock*> outputs;
  while(!inputs.empty()) {
    IntermediateBlock* tmp = inputs.front();
    outputs.push_back(JumpToLocation(tmp));
    // delete old block
    inputs.erase(inputs.begin());
    delete tmp;
    tmp = nullptr;
  }

  return outputs;
}

std::vector<IntermediateBlock*> ItermediateOptimizer::OptimizeMethod(std::vector<IntermediateBlock*> inputs)
{
  // helper to run a pass on all blocks
  auto RunPass = [](std::vector<IntermediateBlock*>& in_blocks,
                    std::function<IntermediateBlock*(IntermediateBlock*)> pass_fn) {
    std::vector<IntermediateBlock*> out_blocks;
    while(!in_blocks.empty()) {
      IntermediateBlock* tmp = in_blocks.front();
      out_blocks.push_back(pass_fn(tmp));
      in_blocks.erase(in_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }
    in_blocks = std::move(out_blocks);
  };

  // 0.0 - clean up jumps (always)
#ifdef _DEBUG
  GetLogger() << L"  Clean up jumps..." << std::endl;
#endif
  RunPass(inputs, [this](IntermediateBlock* b) { return CleanJumps(b); });

  // 0.0 - remove useless instructions (always)
#ifdef _DEBUG
  GetLogger() << L"  Clean up Instructions..." << std::endl;
#endif
  RunPass(inputs, [this](IntermediateBlock* b) { return RemoveUselessInstructions(b); });

  // 0.1 - dead block elimination (s1+)
  if(optimization_level > 0) {
#ifdef _DEBUG
    GetLogger() << L"  Dead block elimination..." << std::endl;
#endif
    RunPass(inputs, [this](IntermediateBlock* b) { return DeadBlockElimination(b); });
  }

  if(is_lib || optimization_level < 1) {
    return inputs;
  }

  // 0.5 - tail call optimization (s1+)
#ifdef _DEBUG
  GetLogger() << L"  Tail call optimization..." << std::endl;
#endif
  inputs = TailCallOpt(inputs);

  // single iteration for now; double iteration showed regression due to overhead
  const int num_iterations = 1;

  for(int iter = 0; iter < num_iterations; ++iter) {
#ifdef _DEBUG
    if(num_iterations > 1) {
      GetLogger() << L"  === Optimization iteration " << (iter + 1) << L" ===" << std::endl;
    }
#endif

    // 1.1 - getter/setter inlining (s1+)
#ifdef _DEBUG
    GetLogger() << L"  Getter/setter inlining..." << std::endl;
#endif
    RunPass(inputs, [this](IntermediateBlock* b) { return InlineSettersGetters(b); });

    // 1.4 - constant propagation (s1+)
#ifdef _DEBUG
    GetLogger() << L"  Constant propagation..." << std::endl;
#endif
    RunPass(inputs, [this](IntermediateBlock* b) { return ConstantProp(b); });

    // 1.5 - dead store (s1+)
#ifdef _DEBUG
    GetLogger() << L"  Dead store..." << std::endl;
#endif
    RunPass(inputs, [this](IntermediateBlock* b) { return DeadStore(b); });

    // 1.6 - common subexpression elimination (s2+)
    if(optimization_level > 1) {
#ifdef _DEBUG
      GetLogger() << L"  CSE..." << std::endl;
#endif
      RunPass(inputs, [this](IntermediateBlock* b) { return CSE(b); });
    }

    // 1.65 - loop-invariant code motion (s2+)
    if(optimization_level > 1) {
#ifdef _DEBUG
      GetLogger() << L"  LICM..." << std::endl;
#endif
      RunPass(inputs, [this](IntermediateBlock* b) { return LICM(b); });
    }

    // 1.7 - fold integers (s1+)
#ifdef _DEBUG
    GetLogger() << L"  Folding integers..." << std::endl;
#endif
    RunPass(inputs, [this](IntermediateBlock* b) { return FoldIntConstants(b); });

    // 1.7 - fold floats (s1+)
#ifdef _DEBUG
    GetLogger() << L"  Folding floats..." << std::endl;
#endif
    RunPass(inputs, [this](IntermediateBlock* b) { return FoldFloatConstants(b); });

    // 2.1 - strength reduction (s2+)
    if(optimization_level > 1) {
#ifdef _DEBUG
      GetLogger() << L"  Strength reduction..." << std::endl;
#endif
      RunPass(inputs, [this](IntermediateBlock* b) { return StrengthReduction(b); });
    }

    // 3.1 - instruction replacement (s3)
    if(optimization_level > 2) {
#ifdef _DEBUG
      GetLogger() << L"  Instruction replacement..." << std::endl;
#endif
      RunPass(inputs, [this](IntermediateBlock* b) { return InstructionReplacement(b); });
    }

    // 3.15 - peephole optimization (s3)
    if(optimization_level > 2) {
#ifdef _DEBUG
      GetLogger() << L"  Peephole optimization..." << std::endl;
#endif
      RunPass(inputs, [this](IntermediateBlock* b) { return PeepholeOptimize(b); });
    }

    // 3.2 - dead code elimination (s2+)
    if(optimization_level > 1) {
#ifdef _DEBUG
      GetLogger() << L"  Dead code elimination..." << std::endl;
#endif
      RunPass(inputs, [this](IntermediateBlock* b) { return DeadCodeElim(b); });
    }
  }

  return inputs;
}

IntermediateBlock* ItermediateOptimizer::RemoveUselessInstructions(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::deque<IntermediateInstruction*> working_stack;
  
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_INT_VAR:
      if(instr->GetOperand2() == LOCL) {
        working_stack.push_front(instr);
      }
      else {
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
      }
      break;
      
    case STOR_INT_VAR:
      if(instr->GetOperand2() == LOCL && !working_stack.empty() && 
         working_stack.front()->GetType() == LOAD_INT_VAR) {
        if(instr->GetOperand() == working_stack.front()->GetOperand()) {
          working_stack.pop_front();
        }
        else {
          while(!working_stack.empty()) {
            outputs->AddInstruction(working_stack.back());
            working_stack.pop_back();
          }
          outputs->AddInstruction(instr);
        }
      }
      else {
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
      }
      break;
     
    default:
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  
  return outputs; 
}

IntermediateBlock* ItermediateOptimizer::CleanJumps(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::deque<IntermediateInstruction*> working_stack;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case JMP:
      working_stack.push_front(instr);
      break;

    case LBL:
      // ignore jump to next instruction
      if(!working_stack.empty() && working_stack.front()->GetType() == JMP &&
         working_stack.front()->GetOperand() == instr->GetOperand()) {
        const bool is_conditional = working_stack.front()->GetOperand2() >= 0;
        frontend::Statement* jmp_stmt = working_stack.front()->GetStatement();
        working_stack.pop_front();
        // a conditional jump pops its condition; keep the stack balanced
        if(is_conditional) {
          working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(jmp_stmt, cur_line_num, POP_INT));
        }
      }
      // add back in reverse order
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;

    default:
      // add back in reverse order
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  return outputs;
}

IntermediateBlock* ItermediateOptimizer::InlineSettersGetters(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    if(instr->GetType() == MTHD_CALL) {
      // The callee may not be in this program's map at all: ids read out of a
      // .obl are local to that library. Unresolvable means "not inlinable",
      // never a dereference of end() (#958).
      IntermediateClass* klass_called = program->GetClass(static_cast<int>(instr->GetOperand()));
      IntermediateMethod* mthd_called = klass_called ? klass_called->GetMethod(static_cast<int>(instr->GetOperand2())) : nullptr;
      int status = CanInlineSetterGetter(mthd_called);
      // The inlined instructions must belong to the call's statement, not the
      // callee's: DeadStoreEdit finds the start of a dead assignment by walking
      // back over instructions of the same statement. A foreign statement
      // stopped that walk mid-expression, leaving operands on the stack (the
      // interpreter then read them as the caller's values; see
      // programs/regression/opt_dead_store_stack_balance.obs).
      IntermediateFactory* factory = IntermediateFactory::Instance();
      //  getter instance pattern
      if(status == 0) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), instrs[1]));
      }
      // getter instance pattern
      else if(status == 1) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), cur_line_num, POP_INT));
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), instrs[0]));
      }
      // character print pattern
      else if(status == 2) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), cur_line_num, POP_INT));
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), instrs[2]));
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), instrs[3]));
      }
      else if(status == 3) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(factory->MakeInstruction(instr->GetStatement(), instrs[3]));
      }
      else {
        outputs->AddInstruction(instr);
      }
    }
    else {
      outputs->AddInstruction(instr);
    }
  }
  
  return outputs;
}

IntermediateBlock* ItermediateOptimizer::InstructionReplacement(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::deque<IntermediateInstruction*> working_stack;

  size_t i = 0;
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  while(i < input_instrs.size() && (input_instrs[i]->GetType() == STOR_INT_VAR ||
                                    input_instrs[i]->GetType() == STOR_FLOAT_VAR)) {
    outputs->AddInstruction(input_instrs[i++]);
  }

  for(; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
      ReplacementInstruction(instr, working_stack, outputs);
      break;

    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
      if(!working_stack.empty() && (working_stack.front()->GetType() == STOR_INT_VAR || working_stack.front()->GetType() == STOR_FLOAT_VAR)) {
        // order matters...
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
      }
      else {
        working_stack.push_front(instr);
      }
      break;
      
    default:
      // order matters...
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::ReplacementInstruction(IntermediateInstruction* instr, std::deque<IntermediateInstruction*> &working_stack, IntermediateBlock* outputs)
{
  if(!working_stack.empty()) {
    IntermediateInstruction* top_instr = working_stack.front();
    if(top_instr->GetType() == STOR_INT_VAR && instr->GetType() == LOAD_INT_VAR &&
       instr->GetOperand() == top_instr->GetOperand() &&
       instr->GetOperand2() == top_instr->GetOperand2()) {
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, COPY_INT_VAR, top_instr->GetOperand(), top_instr->GetOperand2()));
      working_stack.pop_front();
    } 
    else if(top_instr->GetType() == STOR_FLOAT_VAR && instr->GetType() == LOAD_FLOAT_VAR &&
            instr->GetOperand() == top_instr->GetOperand() &&
            instr->GetOperand2() == top_instr->GetOperand2()) {
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, COPY_FLOAT_VAR, top_instr->GetOperand(), top_instr->GetOperand2()));
      working_stack.pop_front();
    } 
    else {
      // order matters...
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
    }
  } 
  else {
    outputs->AddInstruction(instr);
  }
}

bool ItermediateOptimizer::CanInlineMethod(IntermediateMethod* mthd_called, [[maybe_unused]] std::set<IntermediateMethod*>& inlined_mthds, std::set<int>& lbl_jmp_offsets)
{
  // an unresolved callee cannot be inspected, let alone inlined
  if(!mthd_called) {
    return false;
  }

  // Shift this body's labels into a region of their own.
  //
  // This used to advance the offset only when the SAME method was inlined a
  // second time, on the reasoning that that is when labels repeat. They repeat
  // between DIFFERENT methods too: a method's labels are numbered from its own
  // start, so any two callees containing the same construct carry the same
  // numbers. Two helpers that each evaluate `->Abs()` is enough. Inlined under
  // one shared offset, the second body's LBL lands on the first body's LBL, the
  // second body's JMP resolves to the first, and the result is a backward jump
  // into already-executed code -- an infinite loop in a program that is correct
  // at every lower optimization level (#1037).
  //
  // Advanced unconditionally, so each inlined body is isolated from every other
  // one rather than only from a copy of itself. A candidate that is rejected
  // below simply leaves a gap in the numbering, which costs nothing: the offset
  // only has to be unique, not dense. It must be settled before the conflict
  // check further down, which tests the SHIFTED operands.
  jump_inline_offset += JUMP_OFF_INC;

  // don't inline recursive calls
  if(mthd_called == current_method) {
    return false;
  }

  // don't inline parameter calls
  if(mthd_called->GetName().find(current_method->GetName()) != std::string::npos) {
    return false;
  }

  // don't inline method calls for primitive objects
  const std::wstring cls_name_str = mthd_called->GetClass()->GetName();
  if(StartsWith(cls_name_str, L"System.Base:") /* || cls_name_str.find(L'$') != std::wstring::npos */) {
    return false;
  }

  if(current_method->GetSpace() + mthd_called->GetSpace() > LOCL_INLINE_MEM_MAX) {
    return false;
  }

  // ignore constructors
  const std::wstring called_mthd_name = mthd_called->GetName();
  if(called_mthd_name.find(L":New:") != std::wstring::npos) {
    return false;
  }

  // don't inline into "main": it is compiled on entry only when it holds a loop
  // (docs/JIT_ENTRY_COMPILE_DESIGN.md), a loop-free Main is still interpreted, and
  // inlining into an interpreted caller only hides a callee's `native` request
  const std::wstring curr_mthd_name = current_method->GetName();
  if(curr_mthd_name.find(L":Main:o.System.String*,") != std::wstring::npos) {
    return false;
  }

  // don't inline a 'native' method into a method that isn't one. The inlined copy
  // runs in whatever engine its caller does, and a caller the auto-JIT never
  // compiles -- a thread's Run is called once per thread, a driver a handful of
  // times -- interprets it: the loop the user marked for the JIT ran ~50x slower
  // on every worker thread (found 2026-09-09; see jit_native_inline.obs). A
  // native caller is compiled whole, so inlining into it changes nothing.
  if(mthd_called->IsNative() && !current_method->IsNative()) {
    return false;
  }

  // check instructions
  std::vector<IntermediateBlock*> mthd_called_blocks = mthd_called->GetBlocks();
  if(mthd_called_blocks.empty()) {
    return false;
  }

  std::vector<IntermediateInstruction*> mthd_called_instrs = mthd_called_blocks[0]->GetInstructions();

  // must have at least an instruction, non-virtual
  if(mthd_called_instrs.size() < 2) {
    return false;
  }

  bool found_rtrn = false;
  for(size_t j = 0; j < mthd_called_instrs.size(); ++j) {
    IntermediateInstruction* mthd_called_instr = mthd_called_instrs[j];
    switch(mthd_called_instr->GetType()) {
    case instructions::LOAD_INT_VAR:
    case instructions::STOR_INT_VAR:
    case instructions::COPY_INT_VAR:
    case instructions::LOAD_FLOAT_VAR:
    case instructions::STOR_FLOAT_VAR:
    case instructions::COPY_FLOAT_VAR:
    case instructions::LOAD_FUNC_VAR:
    case instructions::STOR_FUNC_VAR:
    case instructions::COPY_FUNC_VAR:
      // if the method/function is in another class it must only have local references
      if(mthd_called_instr->GetOperand2() != LOCL) {
        return false;
      }
      break;

      // ignore special instructions
    case instructions::TRAP:
    case instructions::TRAP_RTRN:
    case instructions::CPY_BYTE_ARY:
    case instructions::CPY_CHAR_ARY:
    case instructions::CPY_INT_ARY:
    case instructions::CPY_FLOAT_ARY:
    case instructions::ZERO_BYTE_ARY:
    case instructions::ZERO_CHAR_ARY:
    case instructions::ZERO_INT_ARY:
    case instructions::ZERO_FLOAT_ARY:
    case instructions::EXT_LIB_LOAD:
    case instructions::EXT_LIB_UNLOAD:
    case instructions::EXT_LIB_FUNC_CALL:
    case instructions::THREAD_JOIN:
    case instructions::THREAD_SLEEP:
    case instructions::THREAD_MUTEX:
    case instructions::CRITICAL_START:
    case instructions::CRITICAL_END:
    case instructions::TRY_START:
    case instructions::TRY_END:
    case instructions::LIB_NEW_OBJ_INST:
    case instructions::LIB_MTHD_CALL:
    case instructions::LIB_OBJ_INST_CAST:
    case instructions::LIB_FUNC_DEF:
      return false;
      
    case instructions::LOAD_CLS_MEM:
      if(mthd_called->GetClass() != current_method->GetClass()) {
        return false;
      }
      break;

      // look for conflicting jump offsets (check that the shifted label won't land on an outer method label)
    case instructions::LBL:
    case instructions::JMP:
    case instructions::JMP_TABLE_SLOT:
      if(lbl_jmp_offsets.find(static_cast<int>(mthd_called_instr->GetOperand() + jump_inline_offset)) != lbl_jmp_offsets.end()) {
        return false;
      }
      break;

    case instructions::JMP_TABLE:
      if(lbl_jmp_offsets.find(static_cast<int>(mthd_called_instr->GetOperand3() + jump_inline_offset)) != lbl_jmp_offsets.end()) {
        return false;
      }
      break;

    case instructions::RTRN:
      if(found_rtrn) {
        return false;
      }
      found_rtrn = true;
      break;

    default:
      break;
    }
  }

  // The callee's and/or temporary becomes a declared local of this frame, and a
  // declaration has one type. Unless the temporary can be declared as what it
  // holds, the body stays a call (InlinedAndOrType).
  if(mthd_called->HasAndOr() && InlinedAndOrType(mthd_called) == IntermediateMethod::AND_OR_TEMP_UNKNOWN) {
    return false;
  }

  // A body with no RTRN never returns: a tail call turned into a loop leaves by
  // its backward jump or by an error. The inliner continues the caller where the
  // callee's RTRN was, so such a body was spliced in without its loop and left
  // the caller's stack unbalanced -- a crash at s3 even interpreted (#1269).
  return found_rtrn;
}

/****************************
 * The declaration an inlined method's and/or temporary takes in its caller's
 * frame, or AND_OR_TEMP_UNKNOWN when none fits and the method must not be
 * inlined.
 *
 * In its own frame the temporary (local id 0) has no declaration and the
 * collector treats it specially: the interpreter reads it as a conservative
 * root, a compiled method carries it through a typed spill slot. Inlined, it is
 * one more declared local, and it was always declared Int. But it is not only a
 * Bool: 'c ? a : b' over objects joins there, so does an Otherwise() receiver
 * and a lambda's closure. Declared Int, such a reference
 *   - is typed Int by the reference maps wherever it is loaded, so the word the
 *     join leaves on the operand stack is no root unless a later use happens to
 *     say "reference": 'Pick(c, a, b) = Same(a)', with a collection inside
 *     Same(), compared the address 'a' had before it moved and answered false
 *     (gc_inline_conditional_temp.obs: wrong at -opt s3 in the interpreter and
 *     the JIT, right at s0), and an object nothing else holds is freed;
 *   - is not seen at all while it sits in the slot, which an interpreted thread
 *     parked at a safepoint between the store and the load exposes.
 *
 * So the temporary is declared as what the emitter recorded it holds
 * (IntermediateEmitter::ClassifyAndOrTemp): Int for values, one reference type
 * when every store is a reference of that kind. A temporary that holds both, or
 * references of two kinds, or that the emitter could not classify (Otherwise(),
 * a closure, a library built before this was recorded) has no declaration that
 * is right, and the method is not inlined. A method that never writes its
 * temporary -- the usual case: '&' and '|' in a condition compile to branches
 * -- is inlined whatever was recorded.
 ****************************/
int ItermediateOptimizer::InlinedAndOrType(IntermediateMethod* mthd_called)
{
  std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
  if(blocks.empty()) {
    return IntermediateMethod::AND_OR_TEMP_UNKNOWN;
  }
  std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();

  // the body's stores to the temporary, and whether any plainly stores a
  // non-zero literal: a value, whatever was recorded
  bool has_store = false;
  bool stores_literal = false;
  for(size_t i = 0; i < instrs.size(); ++i) {
    IntermediateInstruction* instr = instrs[i];
    switch(instr->GetType()) {
    case STOR_INT_VAR:
    case COPY_INT_VAR:
      if(instr->GetOperand() == 0 && instr->GetOperand2() == LOCL) {
        has_store = true;
        if(i > 0 && instrs[i - 1]->GetType() == LOAD_INT_LIT && instrs[i - 1]->GetOperand7() != 0) {
          stores_literal = true;
        }
        else if(i > 0 && instrs[i - 1]->GetType() == LOAD_CHAR_LIT) {
          stores_literal = true;
        }
      }
      break;

    case STOR_FLOAT_VAR:
    case COPY_FLOAT_VAR:
    case STOR_FUNC_VAR:
    case COPY_FUNC_VAR:
      if(instr->GetOperand() == 0 && instr->GetOperand2() == LOCL) {
        has_store = true;
        stores_literal = true;
      }
      break;

    default:
      break;
    }
  }

  if(!has_store) {
    return INT_PARM;
  }

  const int type = mthd_called->GetAndOrTempType();
  switch(type) {
  case INT_PARM:
    return INT_PARM;

  case OBJ_PARM:
  case BYTE_ARY_PARM:
  case CHAR_ARY_PARM:
  case INT_ARY_PARM:
  case FLOAT_ARY_PARM:
  case OBJ_ARY_PARM:
    // recorded as a reference, yet a value is stored: believe neither
    return stores_literal ? IntermediateMethod::AND_OR_TEMP_UNKNOWN : type;

  default:
    return IntermediateMethod::AND_OR_TEMP_UNKNOWN;
  }
}

int ItermediateOptimizer::CanInlineSetterGetter(IntermediateMethod* mthd_called)
{
  // an unresolved callee cannot be inspected, let alone inlined
  if(!mthd_called) {
    return -1;
  }

  if(current_method == mthd_called) {
    return -1;
  };

  // ignore interfaces
  std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
  if(blocks.size() < 1) {
    return -1;
  }

  if(mthd_called->GetNumParams() == 0) {
    std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
    //
    // getter instance pattern
    //
    if(instrs.size() == 3) {
      if(instrs[0]->GetType() == LOAD_INST_MEM &&
         instrs[1]->GetOperand2() == INST && (instrs[1]->GetType() == LOAD_INT_VAR ||
         instrs[1]->GetType() == LOAD_FLOAT_VAR) &&
         instrs[2]->GetType() == RTRN) {
        return 0;
      }
      return -1;
    }
    //
    // getter literal pattern
    //
    else if(instrs.size() == 2) {
      if((instrs[0]->GetType() == LOAD_INT_LIT || instrs[0]->GetType() == LOAD_FLOAT_LIT) &&
         instrs[1]->GetType() == RTRN) {
        return 1;
      }
      return -1;
    }
  }
  else if(mthd_called->GetNumParams() == 1) {
    std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
    if(instrs.size() == 5) {
      //
      // character print pattern
      //
      if(instrs[0]->GetType() == STOR_INT_VAR && instrs[0]->GetOperand() == 0 && instrs[0]->GetOperand2() == LOCL &&
         instrs[1]->GetType() == LOAD_INT_VAR && instrs[1]->GetOperand() == 0 && instrs[1]->GetOperand2() == LOCL &&
         instrs[2]->GetType() == LOAD_INT_LIT && instrs[2]->GetOperand() == -3984 &&
         instrs[3]->GetType() == TRAP && instrs[3]->GetOperand() == 2 &&
         instrs[4]->GetType() == RTRN) {
        return 2;
      }
      //
      // setter instance pattern
      //
      else if(instrs[0]->GetType() == STOR_INT_VAR && instrs[0]->GetOperand() == 0 && instrs[0]->GetOperand2() == LOCL &&
              instrs[1]->GetType() == LOAD_INT_VAR && instrs[1]->GetOperand() == 0 && instrs[1]->GetOperand2() == LOCL &&
              instrs[2]->GetType() == LOAD_INST_MEM &&
              instrs[3]->GetType() == STOR_INT_VAR && instrs[3]->GetOperand2() == INST &&
              instrs[4]->GetType() == RTRN) {
        return 3;
      }
      else if(instrs[0]->GetType() == STOR_FLOAT_VAR && instrs[0]->GetOperand() == 0 && instrs[0]->GetOperand2() == LOCL &&
              instrs[1]->GetType() == LOAD_FLOAT_VAR && instrs[1]->GetOperand() == 0 && instrs[1]->GetOperand2() == LOCL &&
              instrs[2]->GetType() == LOAD_INST_MEM &&
              instrs[3]->GetType() == STOR_FLOAT_VAR && instrs[3]->GetOperand2() == INST &&
              instrs[4]->GetType() == RTRN) {
        return 3;
      }

      return -1;
    }
  }

  return -1;
}

//
// ------------------- Integer identities and strength reduction -------------------
//
// Operands are stack code: a binary operator pops the left operand (pushed
// LAST, on top) and the right operand (pushed first), so `x - 3` is
// `LOAD_INT_LIT 3; <x>; SUB_INT`. An operand is a "simple span" when it is
// straight-line code made only of local Int loads, literals and non-calling
// integer operators; such a span is found by walking backwards and counting how
// many values are still owed. Nothing here crosses a label, jump or call, so a
// literal found directly below a span is exactly that operator's right operand.
//
// Integer arithmetic wraps and shift counts are masked (shared/int_ops.h), so:
//   x + 0, x - 0, x * 1, x or 0, x xor 0, x and -1, x << 0, x >> 0   ->  x
//   x * 0, x and 0                      -> 0 (x is still evaluated, then dropped)
//   x * 2^k                             -> x << k           (exact modulo 2^64)
//   (x + a) + b, (x * a) * b, and/or/xor chains            -> one literal
// Division and remainder are left alone: an arithmetic shift rounds toward
// negative infinity where `/` truncates toward zero, and the stack machine has
// no way to reuse the dividend for the sign correction.
namespace {
  // how many operands the instruction pops when it is allowed inside a simple
  // span; -1 when it is not allowed. Every allowed instruction pushes one value.
  int SpanPops(IntermediateInstruction* instr)
  {
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      return 0;

    case LOAD_INT_VAR:
      return instr->GetOperand2() == LOCL ? 0 : -1;

    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
    case SHL_INT:
    case SHR_INT:
      return 2;

    default:
      return -1;
    }
  }

  // index of the first instruction of the operand that ends at `end`, or -1
  long SpanStart(std::vector<IntermediateInstruction*>& out, long end)
  {
    long owed = 1;
    for(long j = end; j >= 0; --j) {
      const int pops = SpanPops(out[j]);
      if(pops < 0) {
        return -1;
      }
      owed += pops - 1;
      if(owed == 0) {
        return j;
      }
    }

    return -1;
  }

  bool IsIntLit(IntermediateInstruction* instr)
  {
    return instr->GetType() == LOAD_INT_LIT;
  }

  bool IsCommutative(InstructionType type)
  {
    return type == ADD_INT || type == MUL_INT || type == BIT_AND_INT || type == BIT_OR_INT || type == BIT_XOR_INT;
  }

  // push `instr` (a binary integer operator) after simplifying it against the
  // operands already in `out`
  void EmitIntOp(std::vector<IntermediateInstruction*>& out, IntermediateInstruction* instr, int line)
  {
    IntermediateFactory* factory = IntermediateFactory::Instance();

    while(true) {
      const InstructionType type = instr->GetType();
      switch(type) {
      case ADD_INT:
      case SUB_INT:
      case MUL_INT:
      case BIT_AND_INT:
      case BIT_OR_INT:
      case BIT_XOR_INT:
      case SHL_INT:
      case SHR_INT:
        break;

      default:
        out.push_back(instr);
        return;
      }

      const long top = (long)out.size() - 1;
      if(top < 1) {
        out.push_back(instr);
        return;
      }

      long start = SpanStart(out, top);
      if(start < 0) {
        out.push_back(instr);
        return;
      }

      // a literal LEFT operand of a commutative operator moves under the other
      // operand, so every literal operand is the right one below
      if(IsCommutative(type) && start == top && IsIntLit(out[top]) && top >= 1 && !IsIntLit(out[top - 1])) {
        const long other = SpanStart(out, top - 1);
        if(other >= 0) {
          IntermediateInstruction* lit = out[top];
          out.erase(out.begin() + top);
          out.insert(out.begin() + other, lit);
          start = other + 1;
        }
      }

      if(start < 1 || !IsIntLit(out[start - 1])) {
        out.push_back(instr);
        return;
      }

      const long lit_pos = start - 1;
      const INT64_VALUE c = out[lit_pos]->GetOperand7();
      const bool single = (start == top);   // the other operand is one instruction

      switch(type) {
      case ADD_INT:
      case SUB_INT:
      case BIT_OR_INT:
      case BIT_XOR_INT:
        if(c == 0) {
          out.erase(out.begin() + lit_pos);
          return;
        }
        break;

      case BIT_AND_INT:
        if(c == -1) {
          out.erase(out.begin() + lit_pos);
          return;
        }
        break;

      case SHL_INT:
      case SHR_INT:
        if((c & 63) == 0) {
          out.erase(out.begin() + lit_pos);
          return;
        }
        break;

      default:
        break;
      }

      if(type == MUL_INT || type == BIT_AND_INT) {
        const INT64_VALUE zero = 0;
        if(c == zero) {
          if(single) {
            // a lone load or literal has no effect: drop it with the literal
            out.erase(out.begin() + lit_pos, out.begin() + lit_pos + 2);
            out.push_back(factory->MakeIntLitInstruction(line, zero));
          }
          else {
            out.erase(out.begin() + lit_pos);
            out.push_back(factory->MakeInstruction(line, POP_INT));
            out.push_back(factory->MakeIntLitInstruction(line, zero));
          }
          return;
        }
      }

      if(type == MUL_INT && c == 1) {
        out.erase(out.begin() + lit_pos);
        return;
      }

      // (y OP a) OP b -> y OP (a OP b) for + * and or xor: the other operand is
      // itself a literal-then-operand expression of the same operator
      if(top - 1 > start && out[top]->GetType() != LOAD_INT_LIT && IsCommutative(type)) {
        IntermediateInstruction* inner = out[top];
        const InstructionType inner_type = inner->GetType();
        const bool same = inner_type == type;
        // (y << k) is y * 2^k, so a multiplication folds into it as well
        const bool shifted = type == MUL_INT && inner_type == SHL_INT;
        if((same || shifted) && IsIntLit(out[start])) {
          const long y = SpanStart(out, top - 1);
          if(y == start + 1) {
            const INT64_VALUE a = out[start]->GetOperand7();
            INT64_VALUE merged = 0;
            bool ok = true;
            if(shifted) {
              // the shift count is the literal below the operand: a mod 64
              merged = (a >= 0 && a <= 62) ? objeck_int::Mul(c, (INT64_VALUE)1 << a) : 0;
              ok = (a >= 0 && a <= 62);
            }
            else {
              switch(type) {
              case ADD_INT:     merged = objeck_int::Add(c, a); break;
              case MUL_INT:     merged = objeck_int::Mul(c, a); break;
              case BIT_AND_INT: merged = c & a; break;
              case BIT_OR_INT:  merged = c | a; break;
              default:          merged = c ^ a; break;
              }
            }
            if(ok) {
              // [lit c][lit a][y..][inner]  ->  [lit merged][y..][op]
              out.pop_back();
              out.erase(out.begin() + start);
              out[lit_pos] = factory->MakeIntLitInstruction(line, merged);
              instr = shifted ? factory->MakeInstruction(line, MUL_INT) : inner;
              continue;
            }
          }
        }
      }

      // multiplication by a power of two is a shift
      if(type == MUL_INT && c >= 2 && (c & (c - 1)) == 0) {
        int k = 0;
        while(((INT64_VALUE)1 << k) != c) {
          k++;
        }
        out[lit_pos] = factory->MakeIntLitInstruction(line, (INT64_VALUE)k);
        out.push_back(factory->MakeInstruction(line, SHL_INT, (long)k));
        return;
      }

      out.push_back(instr);
      return;
    }
  }

  std::vector<IntermediateInstruction*> SimplifyIntOps(std::vector<IntermediateInstruction*> in, int line)
  {
    std::vector<IntermediateInstruction*> out;
    out.reserve(in.size());
    for(IntermediateInstruction* instr : in) {
      EmitIntOp(out, instr, line);
    }

    return out;
  }
}

IntermediateBlock* ItermediateOptimizer::StrengthReduction(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::deque<IntermediateInstruction*> working_stack;

  std::vector<IntermediateInstruction*> input_instrs = SimplifyIntOps(inputs->GetInstructions(), cur_line_num);
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      working_stack.push_front(instr);
      break;

    case LOAD_INT_VAR:
      working_stack.push_front(instr);
      break;

    // DIV_INT is not reduced: an arithmetic right shift rounds toward negative
    // infinity and division truncates toward zero, so -7 / 2 would give -4. Both
    // JITs already turn a constant divisor into a biased shift or a multiply.
    case MUL_INT:
      CalculateReduction(instr, working_stack, outputs);
      break;

    default:
      // order matters...
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::CalculateReduction(IntermediateInstruction* instr, std::deque<IntermediateInstruction*> &working_stack, IntermediateBlock* outputs)
{
  if(working_stack.size() > 1) {
    IntermediateInstruction* top_instr = working_stack.front();
    working_stack.pop_front();
    if(top_instr->GetType() == LOAD_INT_LIT) {
      if(working_stack.front()->GetType() == LOAD_INT_VAR && top_instr->GetOperand2() == LOCL) {
        ApplyReduction(top_instr, instr, top_instr, working_stack, outputs);
      } 
      else {
        AddBackReduction(instr, top_instr, working_stack, outputs);
      }
    } 
    else if(working_stack.front()->GetType() == LOAD_INT_LIT) {
      if(top_instr->GetType() == LOAD_INT_VAR && top_instr->GetOperand2() == LOCL) {
        ApplyReduction(working_stack.front(), instr, top_instr, working_stack, outputs);
      } 
      else {
        AddBackReduction(instr, top_instr, working_stack, outputs);
      }
    } 
    else {
      AddBackReduction(instr, top_instr, working_stack, outputs);
    }
  } 
  else {
    // order matters...
    while(!working_stack.empty()) {
      outputs->AddInstruction(working_stack.back());
      working_stack.pop_back();
    }
    outputs->AddInstruction(instr);
  }
}

void ItermediateOptimizer::ApplyReduction(IntermediateInstruction* test, IntermediateInstruction* instr, IntermediateInstruction* top_instr, std::deque<IntermediateInstruction*>& working_stack, IntermediateBlock* outputs)
{
  int shift = 0;
  switch(test->GetOperand7()) {
  case 2:
    shift = 1;
    break;

  case 4:
    shift = 2;
    break;

  case 8:
    shift = 3;
    break;

  case 16:
    shift = 4;
    break;

  case 32:
    shift = 5;
    break;

  case 64:
    shift = 6;
    break;

  case 128:
    shift = 7;
    break;

  case 256:
    shift = 8;
    break;

  case 512:
    shift = 9;
    break;

  case 1024:
    shift = 10;
    break;

  case 2048:
    shift = 11;
    break;

  case 4096:
    shift = 12;
    break;

  case 8192:
    shift = 13;
    break;

  case 16384:
    shift = 14;
    break;

  case 32768:
    shift = 15;
    break;

    // C64 (brun)
  case 65536:
    shift = 16;
    break;

  default:
    AddBackReduction(instr, top_instr, working_stack, outputs);
    break;
  }

  std::deque<IntermediateInstruction*> rewrite_instrs;
  if(shift) {
    rewrite_instrs.push_back(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, shift));
    // exclude literal
    if(working_stack.front()->GetType() != LOAD_INT_LIT) {
      rewrite_instrs.push_back(working_stack.front());
    }
    if(top_instr->GetType() != LOAD_INT_LIT) {
      rewrite_instrs.push_back(top_instr);
    }
    working_stack.pop_front();
    // only multiplication reaches here (see StrengthReduction)
    rewrite_instrs.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, SHL_INT, (long)shift));
  }

  // add original instructions
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  // add rewritten instructions
  for(size_t i = 0; i < rewrite_instrs.size(); ++i) {
    outputs->AddInstruction(rewrite_instrs[i]);
  }
}

void ItermediateOptimizer::AddBackReduction(IntermediateInstruction* instr, IntermediateInstruction* top_instr,
                                            std::deque<IntermediateInstruction*> &working_stack, IntermediateBlock* outputs)
{
  // order matters...
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }
  outputs->AddInstruction(top_instr);
  outputs->AddInstruction(instr);
}

IntermediateBlock* ItermediateOptimizer::InlineMethod(IntermediateBlock* inputs)
{
  std::set<IntermediateMethod*> inlined_mthds;
  IntermediateBlock* outputs = new IntermediateBlock;
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();

  std::set<int> lbl_jmp_offsets;
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case LBL:
    case JMP:
      lbl_jmp_offsets.insert(static_cast<int>(instr->GetOperand()));
      break;

    case JMP_TABLE:
      lbl_jmp_offsets.insert(static_cast<int>(instr->GetOperand3()));
      break;

    case JMP_TABLE_SLOT:
      lbl_jmp_offsets.insert(static_cast<int>(instr->GetOperand()));
      break;

    default:
      break;
    }
  }
  lbl_jmp_offsets.insert(++unconditional_label);

  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    if(instr->GetType() == MTHD_CALL) {
      // The callee may not be in this program's map at all: ids read out of a
      // .obl are local to that library. Unresolvable means "not inlinable",
      // never a dereference of end() (#958).
      IntermediateClass* klass_called = program->GetClass(static_cast<int>(instr->GetOperand()));
      IntermediateMethod* mthd_called = klass_called ? klass_called->GetMethod(static_cast<int>(instr->GetOperand2())) : nullptr;
      // checked called method to determine if it can be inlined
      if(CanInlineMethod(mthd_called, inlined_mthds, lbl_jmp_offsets)) {
        // calculate offset
        IntermediateDeclarations* current_entries = current_method->GetEntries();
        // count slots, not declarations: a func-ref local takes two, and
        // counting it once put the inlined locals over the caller's last local
        //
        // The appended declarations start at id 'slots' (id 'slots' + 1 when the
        // caller holds the and/or temporary, whose word shifts every declared id
        // up by one): the instance first, then the callee's and/or temporary if it
        // has one, then the callee's own declarations. The callee's instructions
        // already number its temporary id 0 and its locals from id 1 in that case,
        // so they shift by the instance word alone. Only the CALLER's temporary
        // moves the base. Adding one for the callee's temporary as well (as this
        // did) put every inlined local one id above its declaration whenever the
        // caller had none: an inlined Int[] sat in a slot declared Int, the
        // collector never traced it and freed it while live, and the last local
        // landed past the declarations (gc_scoped_local_slot_types.obs).
        int local_instr_offset = 1 + current_entries->GetSlotCount();
        if(current_method->HasAndOr()) {
          local_instr_offset++;
        }

        // adjust local space, in bytes: the instance word, the callee's and/or
        // word, then its locals -- one word for each declaration appended below
        current_method->SetSpace(current_method->GetSpace() + (int)sizeof(INT64_VALUE) +
                                 (mthd_called->HasAndOr() ? (int)sizeof(INT64_VALUE) : 0) + mthd_called->GetSpace());

        // fetch inline instructions for called method
        std::vector<IntermediateBlock*> mthd_called_blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> mthd_called_instrs = mthd_called_blocks[0]->GetInstructions();

        // handle the storing of local instance
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR,
          local_instr_offset - 1, LOCL));

        // A call starts with its locals zeroed -- the interpreter clears the frame
        // and a compiled method's prologue its slots -- so a local declared without
        // a value (`best : Added;`) reads Nil until it is assigned. The compiler
        // emits no store for such a declaration and relies on that. An inlined
        // body's locals are slots of the caller's frame, which nothing cleared
        // between one inlined "call" and the next: a callee in a loop read what the
        // previous iteration had left, so a search that found nothing returned the
        // last call's match (#1464). Zero them here, as the frame would be: the
        // and/or temporary and every local past the parameters, which the callee's
        // first instructions store from the stack. GetNumParams counts words.
        {
          const int callee_base = local_instr_offset + (mthd_called->HasAndOr() ? 1 : 0);
          if(mthd_called->HasAndOr()) {
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, 0));
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR,
              (long)local_instr_offset, LOCL));
          }
          const std::vector<IntermediateDeclaration*> callee_locals = mthd_called->GetEntries()->GetParameters();
          const int param_words = mthd_called->GetNumParams();
          int slot = 0;
          for(size_t j = 0; j < callee_locals.size(); ++j) {
            const instructions::ParamType type = callee_locals[j]->GetType();
            if(slot >= param_words) {
              const long id = callee_base + slot;
              if(type == instructions::FLOAT_PARM) {
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, (FLOAT_VALUE)0.0));
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_FLOAT_VAR, id, LOCL));
              }
              else if(type == instructions::FUNC_PARM) {
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, 0));
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, 0));
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_FUNC_VAR, id, LOCL));
              }
              else {
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, 0));
                outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR, id, LOCL));
              }
            }
            slot += type == instructions::FUNC_PARM ? 2 : 1;
          }
        }

        current_entries->AddParameter(new IntermediateDeclaration(L"", OBJ_PARM));
        if(mthd_called->HasAndOr()) {
          // declared as what it holds, so an object a conditional leaves there
          // is a root (InlinedAndOrType; CanInlineMethod refused the rest)
          current_entries->AddParameter(new IntermediateDeclaration(L"", (instructions::ParamType)InlinedAndOrType(mthd_called)));
        }

        std::vector<IntermediateDeclaration*> entries = mthd_called->GetEntries()->GetParameters();
        for(size_t j = 0; j < entries.size(); ++j) {
          current_entries->AddParameter(new IntermediateDeclaration(entries[j]->GetName(), entries[j]->GetType()));
        }

        // inline instructions
        for(size_t j = 0; j < mthd_called_instrs.size() - 1; ++j) {
          IntermediateInstruction* mthd_called_instr = mthd_called_instrs[j];
          switch(mthd_called_instr->GetType()) {
          case LOAD_INT_VAR:
          case STOR_INT_VAR:
          case COPY_INT_VAR:
          case LOAD_FLOAT_VAR:
          case STOR_FLOAT_VAR:
          case COPY_FLOAT_VAR:
          // A func-ref local is two words at the callee's id and the next; the
          // id shifts with the others. These were pasted unshifted, so an
          // inlined method that took a func-ref parameter stored it over the
          // caller's own locals and called through whatever was there (#763).
          case LOAD_FUNC_VAR:
          case STOR_FUNC_VAR:
          case COPY_FUNC_VAR:
#ifdef _DEBUG
            assert(mthd_called_instr->GetOperand2() == LOCL);
#endif
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, mthd_called_instr->GetType(),
                                    mthd_called_instr->GetOperand() + local_instr_offset, mthd_called_instr->GetOperand2()));
            break;

          case LOAD_INST_MEM:
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, local_instr_offset - 1, LOCL));
            break;

          case JMP:
            lbl_jmp_offsets.insert(static_cast<int>(mthd_called_instr->GetOperand() + jump_inline_offset));
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, mthd_called_instr->GetOperand() + jump_inline_offset,
                                                                                     mthd_called_instr->GetOperand2()));
            break;

          case LBL:
            lbl_jmp_offsets.insert(static_cast<int>(mthd_called_instr->GetOperand() + jump_inline_offset));
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LBL, mthd_called_instr->GetOperand() + jump_inline_offset,
                                                                                     mthd_called_instr->GetOperand2()));
            break;

          case JMP_TABLE:
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP_TABLE,
              mthd_called_instr->GetOperand(), mthd_called_instr->GetOperand2(),
              mthd_called_instr->GetOperand3() + jump_inline_offset));
            break;

          case JMP_TABLE_SLOT:
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP_TABLE_SLOT,
              mthd_called_instr->GetOperand() + jump_inline_offset));
            break;

          default:
            outputs->AddInstruction(mthd_called_instr);
            break;
          }
        }
        inlined_mthds.insert(mthd_called);
      }
      else {
        outputs->AddInstruction(instr);
      }
    }
    else {
      outputs->AddInstruction(instr);
    }
  }

  return outputs;
}

IntermediateBlock* ItermediateOptimizer::CleanLabelsLocation(IntermediateBlock* inputs)
{
  // remove redundancy labels
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  IntermediateBlock* outputs = new IntermediateBlock;

  // track groups of redundant labls
  std::map<long, IntermediateInstruction*> lbl_ids;
  
  int new_label_id = unconditional_label;
  IntermediateInstruction* new_label_instr = nullptr;
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    // start of duplicate labels
    if(!new_label_instr && instr->GetType() == LBL && i + 1 < input_instrs.size() && input_instrs[i + 1]->GetType() == LBL) {
      new_label_instr = IntermediateFactory::Instance()->MakeInstruction(-1, LBL, -1, -1);
      lbl_ids.insert(std::pair<long, IntermediateInstruction*>(instr->GetOperand(), new_label_instr));
      outputs->AddInstruction(new_label_instr);
    }
    // duplicate labels
    else if(new_label_instr) {
      // duplicate label
      if(instr->GetType() == LBL) {
        lbl_ids.insert(std::pair<long, IntermediateInstruction*>(instr->GetOperand(), new_label_instr));
      }
      // end of duplicate labels
      else {
        new_label_instr->SetOperand((long)++new_label_id);
        new_label_instr = nullptr;

        // add instruction
        outputs->AddInstruction(instr);
      }
    }
    // normal instruction
    else if(!new_label_instr) {
      outputs->AddInstruction(instr);
    }
  }

  // finalize merged label if consecutive LBLs ended the stream (no trailing non-LBL triggered SetOperand)
  if(new_label_instr) {
    new_label_instr->SetOperand((long)++new_label_id);
    new_label_instr = nullptr;
  }

  // update jump for redundant labels
  if(new_label_id != unconditional_label) {
    unconditional_label = new_label_id;
    for(size_t i = 0; i < input_instrs.size(); ++i) {
      IntermediateInstruction* instr = input_instrs[i];

      // update jumps
      if(instr->GetType() == JMP || instr->GetType() == TRY_START
         || instr->GetType() == JMP_TABLE_SLOT) {
        const long jump_id = instr->GetOperand();
        std::map<long, IntermediateInstruction*>::iterator jump_result = lbl_ids.find(jump_id);
        if(jump_result != lbl_ids.end()) {
          instr->SetOperand(jump_result->second->GetOperand());
        }
      }
      if(instr->GetType() == JMP_TABLE) {
        const long def_id = instr->GetOperand3();
        std::map<long, IntermediateInstruction*>::iterator def_result = lbl_ids.find(def_id);
        if(def_result != lbl_ids.end()) {
          instr->SetOperand3(def_result->second->GetOperand());
        }
      }
    }
  }
  
  return outputs;
}

IntermediateBlock* ItermediateOptimizer::JumpToLocation(IntermediateBlock* inputs)
{
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();

  // map labels ids to indexes
  IntermediateBlock* outputs = new IntermediateBlock;
  std::unordered_map<int, int> lbl_offsets;
  // test hook (programs/regression/check_label_fatal.py): source cannot produce a lost or
  // doubled label, so this simulates a bug in label generation to exercise the checks below
  const char* label_fault = nullptr;
#ifdef _WIN32
  char label_fault_buf[16] = {0};   // MSVC rejects getenv (C4996, an error in this build)
  { size_t len = 0; if(getenv_s(&len, label_fault_buf, sizeof(label_fault_buf), "OBJECK_TEST_LABEL_FAULT") == 0 && len > 1) { label_fault = label_fault_buf; } }
#else
  label_fault = getenv("OBJECK_TEST_LABEL_FAULT");
#endif
  bool label_fault_done = false;
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case LBL:
      if(label_fault && !label_fault_done) {
        label_fault_done = true;
        if(!strcmp(label_fault, "drop")) {
          break;
        }
        if(!strcmp(label_fault, "dup")) {
          lbl_offsets.insert(std::pair<int, int>(instr->GetOperand(), (int)i + 1));
        }
      }
      if(!lbl_offsets.insert(std::pair<int, int>(instr->GetOperand(), (int)i + 1)).second) {
        throw CompilerFatal(L"Internal compiler error: duplicate jump label id " + std::to_wstring(instr->GetOperand()));
      }
      break;

    default:
      break;
    }
  }
  
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case JMP: {
      std::unordered_map<int, int>::iterator result = lbl_offsets.find(static_cast<int>(instr->GetOperand()));
      if(result == lbl_offsets.end()) {
        throw CompilerFatal(L"Internal compiler error: unresolved jump label id " + std::to_wstring(instr->GetOperand()));
      }
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, result->second, instr->GetOperand2()));
    }
      break;

    case TRY_START: {
      std::unordered_map<int, int>::iterator result = lbl_offsets.find(static_cast<int>(instr->GetOperand()));
      if(result == lbl_offsets.end()) {
        throw CompilerFatal(L"Internal compiler error: unresolved jump label id " + std::to_wstring(instr->GetOperand()));
      }
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, TRY_START, result->second, instr->GetOperand2()));
    }
      break;

    case JMP_TABLE: {
      std::unordered_map<int, int>::iterator def_result = lbl_offsets.find((int)instr->GetOperand3());
      if(def_result == lbl_offsets.end()) {
        throw CompilerFatal(L"Internal compiler error: unresolved jump table default label id " + std::to_wstring(instr->GetOperand3()));
      }
      const long default_ip = (long)def_result->second;
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP_TABLE,
        instr->GetOperand(), instr->GetOperand2(), default_ip));
    }
      break;

    case JMP_TABLE_SLOT: {
      std::unordered_map<int, int>::iterator slot_result = lbl_offsets.find((int)instr->GetOperand());
      if(slot_result == lbl_offsets.end()) {
        throw CompilerFatal(L"Internal compiler error: unresolved jump table slot label id " + std::to_wstring(instr->GetOperand()));
      }
      const long target_ip = (long)slot_result->second;
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP_TABLE_SLOT, target_ip));
    }
      break;

    default:
      outputs->AddInstruction(instr);
      break;
    }
  }

  return outputs;
}

//
// ------------------- Start: NEW OPTIMIZATIONS -------------------
//

IntermediateBlock* ItermediateOptimizer::DeadStore(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();

  bool done = false;
  size_t start_pos = 0;
  for(size_t i = 0; !done && i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
      start_pos++;
      break;

    default:
      done = true;
      break;
    }
  }

  // A store is dead when no path from it reads its local before another store writes it,
  // and no exception handler of the try region it is in reads it. The control-flow graph
  // answers that: across a join, around a loop, in a handler, and for an instruction that
  // reads the frame without naming a slot (a native call, a thread operation).
  const size_t instr_count = input_instrs.size();
  std::vector<char> is_dead(instr_count, 0);
  bool graph_used = false;
  {
    Cfg cfg(input_instrs);
    if(cfg.WellFormed()) {
      cfg.ComputeLiveness();
      const std::vector<bool> graph_dead = cfg.DeadLocalStores();
      for(size_t i = start_pos; i < instr_count; ++i) {
        if(graph_dead[i]) {
          is_dead[i] = 1;
        }
      }
      graph_used = true;
    }
  }

  // No graph (the list is not one method's well-formed code): the old straight-line rule,
  // a store is dead when the next thing that happens to its local is another store, with no
  // read, jump, label or return first. One backward pass; 'after' holds, per local, what its
  // first later event is, and a jump, label or return forgets everything (a generation
  // count does that in O(1)). That rule ignores that a trap inside a try region can hand
  // control to a handler that reads the local, so it is not used when the list has one.
  bool has_try = false;
  for(size_t i = start_pos; !graph_used && !has_try && i < instr_count; ++i) {
    has_try = input_instrs[i]->GetType() == TRY_START;
  }
  if(!graph_used && !has_try) {
    std::unordered_map<long, std::pair<bool, size_t> > after;
    size_t generation = 0;
    for(size_t i = instr_count; i-- > start_pos;) {
      IntermediateInstruction* instr = input_instrs[i];
      switch(instr->GetType()) {
      case JMP:
      case LBL:
      case RTRN:
        ++generation;
        break;

      case LOAD_INT_VAR:
      case LOAD_FLOAT_VAR:
      case LOAD_FUNC_VAR:
        if(instr->GetOperand2() == LOCL) {
          after[instr->GetOperand()] = std::pair<bool, size_t>(false, generation);
        }
        break;

      case STOR_INT_VAR:
      case STOR_FLOAT_VAR:
      case STOR_FUNC_VAR:
        if(instr->GetOperand2() == LOCL) {
          const auto found = after.find(instr->GetOperand());
          is_dead[i] = found != after.end() && found->second.second == generation && found->second.first;
          after[instr->GetOperand()] = std::pair<bool, size_t>(true, generation);
        }
        break;

      default:
        break;
      }
    }
  }

  // the instructions to drop, marked as they are found (a range per dead store)
  std::vector<char> drop(instr_count, 0);

  for(size_t i = start_pos; i < instr_count; ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
      if(instr->GetStatement() && is_dead[i]) {
        const std::pair<size_t, size_t> dead_store_edit = DeadStoreEdit(i, input_instrs);
        // Only the store is dead, not the expression: removing a statement
        // whose value has effects (a method call, an allocation, a trap, a
        // division that can fault) changed behaviour. `v0 := s->M(p0); v0 := 1;`
        // lost the call to M at s1+ (fuzzer, programs/regression/opt_dead_store_side_effects.obs).
        if(dead_store_edit.first <= dead_store_edit.second &&
           IsEffectFreeRange(dead_store_edit.first, dead_store_edit.second, input_instrs)) {
          for(size_t j = dead_store_edit.first; j <= dead_store_edit.second; ++j) {
            drop[j] = 1;
          }
        }
      }
      break;

    default:
      break;
    }
  }

  // copy instructions
  if(input_instrs.empty()) {
    outputs->AddInstructions(input_instrs);
  }
  else {
    for(size_t i = 0; i < instr_count; ++i) {
      if(!drop[i]) {
        outputs->AddInstruction(input_instrs[i]);
      }
    }
  }

  return outputs;
}
// True when the instructions in [start_pos, end_pos) (the dead store's value,
// excluding the store itself at end_pos) can be dropped without a visible
// difference: no calls, allocations, traps, writes, or operations that can
// raise a runtime error (division, array access, Nil dereference).
bool ItermediateOptimizer::IsEffectFreeRange(size_t start_pos, size_t end_pos, std::vector<IntermediateInstruction*>& input_instrs)
{
  for(size_t i = start_pos; i < end_pos && i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
    case LOAD_CHAR_LIT:
    case LOAD_FLOAT_LIT:
    case LOAD_CLS_MEM:
    case LOAD_INST_MEM:
    case EQL_INT:
    case NEQL_INT:
    case LES_INT:
    case GTR_INT:
    case LES_EQL_INT:
    case GTR_EQL_INT:
    case EQL_FLOAT:
    case NEQL_FLOAT:
    case LES_FLOAT:
    case GTR_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case AND_INT:
    case OR_INT:
    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
    case BIT_NOT_INT:
    case SHL_INT:
    case SHR_INT:
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    // not DIV_FLOAT: a Float division by zero raises, as DIV_INT does (#1178)
    case I2F:
    case F2I:
    case SWAP_INT:
    case POP_INT:
    case POP_FLOAT:
      break;

    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
      // a local, or a field of 'self' or of the class; a field of any other
      // object (an inlined getter) faults when that object is Nil
      if(instr->GetOperand2() != LOCL) {
        if(i == start_pos) {
          return false;
        }
        const InstructionType prev = input_instrs[i - 1]->GetType();
        if(prev != LOAD_INST_MEM && prev != LOAD_CLS_MEM) {
          return false;
        }
      }
      break;

    default:
      return false;
    }
  }

  return true;
}

// What an instruction does to the operand stack, for the instructions
// IsEffectFreeRange accepts; false for any other.
static bool EffectFreeStackUse(IntermediateInstruction* instr, long& pops, long& pushes)
{
  switch(instr->GetType()) {
  case LOAD_INT_LIT:
  case LOAD_CHAR_LIT:
  case LOAD_FLOAT_LIT:
  case LOAD_CLS_MEM:
  case LOAD_INST_MEM:
    pops = 0;
    pushes = 1;
    return true;

  case EQL_INT:
  case NEQL_INT:
  case LES_INT:
  case GTR_INT:
  case LES_EQL_INT:
  case GTR_EQL_INT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case LES_FLOAT:
  case GTR_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_EQL_FLOAT:
  case AND_INT:
  case OR_INT:
  case ADD_INT:
  case SUB_INT:
  case MUL_INT:
  case BIT_AND_INT:
  case BIT_OR_INT:
  case BIT_XOR_INT:
  case SHL_INT:
  case SHR_INT:
  case ADD_FLOAT:
  case SUB_FLOAT:
  case MUL_FLOAT:
    pops = 2;
    pushes = 1;
    return true;

  case BIT_NOT_INT:
  case I2F:
  case F2I:
    pops = 1;
    pushes = 1;
    return true;

  case SWAP_INT:
    pops = 2;
    pushes = 2;
    return true;

  case POP_INT:
  case POP_FLOAT:
    pops = 1;
    pushes = 0;
    return true;

  case LOAD_INT_VAR:
  case LOAD_FLOAT_VAR:
    // a field is read off the object or class beneath it
    pops = instr->GetOperand2() == LOCL ? 0 : 1;
    pushes = 1;
    return true;

  case LOAD_FUNC_VAR:
    // a function reference is two words
    pops = instr->GetOperand2() == LOCL ? 0 : 1;
    pushes = 2;
    return true;

  default:
    return false;
  }
}

// The instructions to remove with a dead store: [first, store], or
// (store + 1, store) when the store has to stay.
//
// They must be exactly the instructions that put the store's words on the
// operand stack. This used to be "the instructions before the store that carry
// its statement", and a tag does not say where a value starts: in a chained
// assignment, `a := b := E`, the inner link's store is tagged with its own
// assignment while E carries the outer statement. The walk back from a dead
// `b` stopped at once, the store went and E stayed. One word too many on that
// path: the loader refuses the method where the path joins another ("stack
// heights differ where control flow joins"), and in straight-line code the
// word stays under everything the method pushes afterwards
// (programs/regression/opt_dead_store_chain.obs).
//
// So the statement's instructions go only when the stack says they are the
// value: from an empty stack they leave the store's words and nothing else.
// When they are not, the value is found by walking back from the store until
// the words it pops are accounted for, across statements. That walk covers
// only instructions IsEffectFreeRange accepts, and gives up on one that also
// pushes a word the code after the store uses.
std::pair<size_t, size_t> ItermediateOptimizer::DeadStoreEdit(size_t start_pos, std::vector<IntermediateInstruction*>& input_instrs)
{
  const std::pair<size_t, size_t> not_found(start_pos + 1, start_pos);
  IntermediateInstruction* dead_store_instr = input_instrs[start_pos];
  if(dead_store_instr->GetOperand2() != LOCL) {
    return not_found;
  }
  // the words the store takes off the stack
  const long words = dead_store_instr->GetType() == STOR_FUNC_VAR ? 2 : 1;

  // The statement's instructions before the store, down to and INCLUDING index
  // 0: a dead store's value can be a method's first instruction
  // (programs/regression/opt_dead_store_first_instr.obs).
  size_t statement_pos = start_pos;
  while(statement_pos > 0 && input_instrs[statement_pos - 1]->GetStatement() == dead_store_instr->GetStatement()) {
    --statement_pos;
  }
  // A statement that does more than compute a value (a call, a jump, another
  // store) keeps its dead store, as it always has.
  if(!IsEffectFreeRange(statement_pos, start_pos, input_instrs)) {
    return not_found;
  }

  // the statement's instructions, run from an empty stack
  long depth = 0;
  bool whole = true;
  for(size_t i = statement_pos; whole && i < start_pos; ++i) {
    long pops;
    long pushes;
    if(!EffectFreeStackUse(input_instrs[i], pops, pushes) || depth < pops) {
      whole = false;
    }
    else {
      depth += pushes - pops;
    }
  }
  if(whole && depth == words) {
    return std::pair<size_t, size_t>(statement_pos, start_pos);
  }

  // not the value, or not all of it: walk back to where the store's words begin
  long need = words;
  size_t pos = start_pos;
  while(need > 0) {
    // running out of instructions means the words came from the caller
    if(pos == 0) {
      return not_found;
    }
    --pos;

    long pops;
    long pushes;
    // more pushed than is still owed: the rest is for the code after the store
    if(!EffectFreeStackUse(input_instrs[pos], pops, pushes) || pushes > need) {
      return not_found;
    }
    need += pops - pushes;
  }

  return std::pair<size_t, size_t>(pos, start_pos);
}

IntermediateBlock* ItermediateOptimizer::ConstantProp(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;

  bool set_int = false;
  INT64_VALUE int_value = 0;

  bool set_float = false;
  double float_value = 0.0;

  std::unordered_map<INT64_VALUE, PropValue> value_prop_map;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
      // int propagation
    case LOAD_INT_LIT:
      outputs->AddInstruction(instr);
      // check for load/store/load pattern, if found it will be optimizing out later as a copy
      if(i + 2 < input_instrs.size()) {
        IntermediateInstruction* next_instr = input_instrs[i + 1];
        IntermediateInstruction* next_next_instr = input_instrs[i + 2];
        if(next_instr->GetType() == STOR_INT_VAR && next_next_instr->GetType() == LOAD_INT_VAR &&
           next_instr->GetOperand() == next_next_instr->GetOperand() &&
           next_instr->GetOperand2() == next_next_instr->GetOperand2()) {
          int_value = instr->GetOperand7();
          set_int = true;
        }
      }
      else {
        int_value = instr->GetOperand7();
        set_int = true;
      }
      break;

    case STOR_INT_VAR:
      outputs->AddInstruction(instr);
      if(instr->GetOperand2() == LOCL) {
        if(set_int) {
          value_prop_map[instr->GetOperand()].int_value = int_value;
        }
        else {
          // Reassigned from a non-constant: invalidate any stale constant for
          // this slot, otherwise a later load is wrongly replaced with the old
          // literal (e.g. x:=5; y:=x; x:=z+w; return x returned 5).
          value_prop_map.erase(instr->GetOperand());
        }
      }
      set_int = set_float = false;
      break;

    case LOAD_INT_VAR: {
      if(instr->GetOperand2() == LOCL) {
        std::unordered_map<INT64_VALUE, PropValue>::iterator result = value_prop_map.find(instr->GetOperand());
        if(result != value_prop_map.end()) {
          // keep the statement (see InlineSettersGetters)
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(instr->GetStatement(), cur_line_num, result->second.int_value));
        }
        else {
          outputs->AddInstruction(instr);
        }
      }
      else {
        outputs->AddInstruction(instr);
      }
      // reset
      set_int = set_float = false;
    }
      break;

      // float propagation
    case LOAD_FLOAT_LIT:
      outputs->AddInstruction(instr);
      // check for load/store/load pattern, if found it will be optimizing out later as a copy
      if(i + 2 < input_instrs.size()) {
        IntermediateInstruction* next_instr = input_instrs[i + 1];
        IntermediateInstruction* next_next_instr = input_instrs[i + 2];
        if(next_instr->GetType() == STOR_FLOAT_VAR && next_next_instr->GetType() == LOAD_FLOAT_VAR &&
           next_instr->GetOperand() == next_next_instr->GetOperand() &&
           next_instr->GetOperand2() == next_next_instr->GetOperand2()) {
          float_value = instr->GetOperand4();
          set_float = true;
        }
      }
      else {
        float_value = instr->GetOperand4();
        set_float = true;
      }
      break;

    case STOR_FLOAT_VAR:
      outputs->AddInstruction(instr);
      if(instr->GetOperand2() == LOCL) {
        if(set_float) {
          value_prop_map[instr->GetOperand()].float_value = float_value;
        }
        else {
          // Reassigned from a non-constant: invalidate any stale constant
          // (see STOR_INT_VAR above).
          value_prop_map.erase(instr->GetOperand());
        }
      }
      set_int = set_float = false;
      break;

    case LOAD_FLOAT_VAR: {
      if(instr->GetOperand2() == LOCL) {
        std::unordered_map<INT64_VALUE, PropValue>::iterator result = value_prop_map.find(instr->GetOperand());
        if(result != value_prop_map.end()) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(instr->GetStatement(), cur_line_num, LOAD_FLOAT_LIT, result->second.float_value));
        }
        else {
          outputs->AddInstruction(instr);
        }
      }
      else {
        outputs->AddInstruction(instr);
      }
      // reset
      set_int = set_float = false;
    }
     break;

     // reset propagation map
    case MTHD_CALL:
    case DYN_MTHD_CALL:
    case JMP:
    case LBL:
    case RTRN:
      outputs->AddInstruction(instr);
      value_prop_map.clear();
      // reset
      set_int = set_float = false;
      break;

    default:
      outputs->AddInstruction(instr);
      // reset
      set_int = set_float = false;
      break;
    }
  }

  return outputs;
}

//
// ------------------- End: NEW OPTIMIZATIONS -------------------
//

IntermediateBlock* ItermediateOptimizer::FoldIntConstants(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::deque<IntermediateInstruction*> working_stack;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      working_stack.push_front(instr);
      break;

    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
      CalculateIntFold(instr, working_stack, outputs);
      break;

    default:
      // add back in reverse order
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::CalculateIntFold(IntermediateInstruction* instr, std::deque<IntermediateInstruction*> &working_stack, IntermediateBlock* outputs)
{
  if(working_stack.size() == 1) {
    outputs->AddInstruction(working_stack.front());
    working_stack.pop_front();
    outputs->AddInstruction(instr);
  } 
  else if(working_stack.size() > 1) {
    IntermediateInstruction* left = working_stack.front();
    working_stack.pop_front();

    IntermediateInstruction* right = working_stack.front();
    working_stack.pop_front();

    switch(instr->GetType()) {
    // folds use shared/int_ops.h, the same semantics the VM executes: wrapping
    // + - *, MIN / -1 = MIN, MIN % -1 = 0
    case ADD_INT: {
      const INT64_VALUE value = objeck_int::Add(left->GetOperand7(), right->GetOperand7());
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case SUB_INT: {
      const INT64_VALUE value = objeck_int::Sub(left->GetOperand7(), right->GetOperand7());
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case MUL_INT: {
      const INT64_VALUE value = objeck_int::Mul(left->GetOperand7(), right->GetOperand7());
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case DIV_INT: {
      // a zero divisor is left to trap at runtime; INT64_MIN / -1 used to be
      // left too (a C++ division faulted the compiler), and now folds to MIN
      if(right->GetOperand7() == 0) {
        // the operation must survive to trap at runtime, and its operands must
        // land in the output BEFORE it -- emitting the instruction here and
        // leaving the literals on the working stack appended them after it,
        // so the division executed against an empty stack and the clean
        // "Divide by zero" trap became a silent crash at default optimization
        working_stack.push_front(right);
        working_stack.push_front(left);
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
        return;
      }
      const INT64_VALUE value = objeck_int::Div(left->GetOperand7(), right->GetOperand7());
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case MOD_INT: {
      // see DIV_INT: a zero divisor traps at runtime; INT64_MIN % -1 folds to 0
      if(right->GetOperand7() == 0) {
        // the operation must survive to trap at runtime, and its operands must
        // land in the output BEFORE it -- emitting the instruction here and
        // leaving the literals on the working stack appended them after it,
        // so the division executed against an empty stack and the clean
        // "Divide by zero" trap became a silent crash at default optimization
        working_stack.push_front(right);
        working_stack.push_front(left);
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
        return;
      }
      const INT64_VALUE value = objeck_int::Mod(left->GetOperand7(), right->GetOperand7());
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;
      
    case BIT_AND_INT: {
      const INT64_VALUE value = left->GetOperand7() & right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case BIT_OR_INT: {
      const INT64_VALUE value = left->GetOperand7() | right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case BIT_XOR_INT: {
      const INT64_VALUE value = left->GetOperand7() ^ right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    default:
      break;
    }
  } 
  else {
    outputs->AddInstruction(instr);
  }
}

IntermediateBlock* ItermediateOptimizer::FoldFloatConstants(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::deque<IntermediateInstruction*> working_stack;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_FLOAT_LIT:
      working_stack.push_front(instr);
      break;

    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
      CalculateFloatFold(instr, working_stack, outputs);
      break;

    default:
      // add back in reverse order
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::CalculateFloatFold(IntermediateInstruction* instr, std::deque<IntermediateInstruction*> &working_stack, IntermediateBlock* outputs)
{
  if(working_stack.size() == 1) {
    outputs->AddInstruction(working_stack.front());
    working_stack.pop_front();
    outputs->AddInstruction(instr);
  } 
  else if(working_stack.size() > 1) {
    IntermediateInstruction* left = working_stack.front();
    working_stack.pop_front();

    IntermediateInstruction* right = working_stack.front();
    working_stack.pop_front();

    switch(instr->GetType()) {
    case ADD_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() + right->GetOperand4();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
      break;

    case SUB_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() - right->GetOperand4();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
      break;

    case MUL_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() * right->GetOperand4();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
      break;
    
    case DIV_FLOAT: {
      // as DIV_INT: the VM raises "Divide by zero" for a Float divisor too, so
      // a zero divisor is left to trap at runtime. Folding it to inf made
      // `1.0 / 0.0` raise at -opt s0 and print inf at s1 and above (#1178).
      if(right->GetOperand4() == 0.0) {
        working_stack.push_front(right);
        working_stack.push_front(left);
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
        return;
      }
      FLOAT_VALUE value = left->GetOperand4() / right->GetOperand4();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
      break;

    default:
      break;
    }
  }
  else {
    outputs->AddInstruction(instr);
  }
}

//
// ------------------- Copy Propagation -------------------
//
IntermediateBlock* ItermediateOptimizer::CopyProp(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;

  // map: variable slot -> source variable slot (for LOCL variables only)
  std::unordered_map<long, long> copy_map;
  // track which variables are int vs float
  std::unordered_map<long, InstructionType> copy_type_map;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    // detect store-after-load pattern for copy tracking
    case STOR_INT_VAR:
      if(instr->GetOperand2() == LOCL && i > 0) {
        IntermediateInstruction* prev = input_instrs[i - 1];
        if(prev->GetType() == LOAD_INT_VAR && prev->GetOperand2() == LOCL) {
          // var[operand] = var[prev->operand] -- track the copy
          copy_map[instr->GetOperand()] = prev->GetOperand();
          copy_type_map[instr->GetOperand()] = LOAD_INT_VAR;
        }
        else {
          // store from non-variable source, invalidate
          copy_map.erase(instr->GetOperand());
        }
        // also invalidate anything that was copied FROM this var
        for(auto it = copy_map.begin(); it != copy_map.end();) {
          if(it->second == instr->GetOperand()) {
            it = copy_map.erase(it);
          }
          else {
            ++it;
          }
        }
      }
      else {
        copy_map.clear();
        copy_type_map.clear();
      }
      outputs->AddInstruction(instr);
      break;

    case STOR_FLOAT_VAR:
      if(instr->GetOperand2() == LOCL && i > 0) {
        IntermediateInstruction* prev = input_instrs[i - 1];
        if(prev->GetType() == LOAD_FLOAT_VAR && prev->GetOperand2() == LOCL) {
          copy_map[instr->GetOperand()] = prev->GetOperand();
          copy_type_map[instr->GetOperand()] = LOAD_FLOAT_VAR;
        }
        else {
          copy_map.erase(instr->GetOperand());
        }
        for(auto it = copy_map.begin(); it != copy_map.end();) {
          if(it->second == instr->GetOperand()) {
            it = copy_map.erase(it);
          }
          else {
            ++it;
          }
        }
      }
      else {
        copy_map.clear();
        copy_type_map.clear();
      }
      outputs->AddInstruction(instr);
      break;

    // replace loads with source variable if copy exists
    case LOAD_INT_VAR:
      if(instr->GetOperand2() == LOCL) {
        auto result = copy_map.find(instr->GetOperand());
        if(result != copy_map.end() && copy_type_map[instr->GetOperand()] == LOAD_INT_VAR) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, result->second, LOCL));
        }
        else {
          outputs->AddInstruction(instr);
        }
      }
      else {
        outputs->AddInstruction(instr);
      }
      break;

    case LOAD_FLOAT_VAR:
      if(instr->GetOperand2() == LOCL) {
        auto result = copy_map.find(instr->GetOperand());
        if(result != copy_map.end() && copy_type_map[instr->GetOperand()] == LOAD_FLOAT_VAR) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_VAR, result->second, LOCL));
        }
        else {
          outputs->AddInstruction(instr);
        }
      }
      else {
        outputs->AddInstruction(instr);
      }
      break;

    // invalidate all copies on control flow changes and calls
    case MTHD_CALL:
    case DYN_MTHD_CALL:
    case JMP:
    case LBL:
    case RTRN:
      outputs->AddInstruction(instr);
      copy_map.clear();
      copy_type_map.clear();
      break;

    default:
      outputs->AddInstruction(instr);
      break;
    }
  }

  return outputs;
}

//
// ------------------- Common Subexpression Elimination -------------------
//
IntermediateBlock* ItermediateOptimizer::CSE(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;

  // key: (opcode, operand1_slot, operand2_slot) -> result variable slot
  struct CSEKey {
    InstructionType op;
    long left_slot;
    long right_slot;

    bool operator==(const CSEKey& other) const {
      return op == other.op && left_slot == other.left_slot && right_slot == other.right_slot;
    }
  };

  struct CSEKeyHash {
    size_t operator()(const CSEKey& k) const {
      size_t h = std::hash<int>()(k.op);
      h ^= std::hash<long>()(k.left_slot) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= std::hash<long>()(k.right_slot) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };

  std::unordered_map<CSEKey, long, CSEKeyHash> cse_map;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  std::deque<IntermediateInstruction*> working_stack;

  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
      if(instr->GetOperand2() == LOCL) {
        working_stack.push_front(instr);
      }
      else {
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
      }
      break;

    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT: {
      if(working_stack.size() >= 2) {
        IntermediateInstruction* right_instr = working_stack.front();
        working_stack.pop_front();
        IntermediateInstruction* left_instr = working_stack.front();
        working_stack.pop_front();

        bool is_left_var = (left_instr->GetType() == LOAD_INT_VAR || left_instr->GetType() == LOAD_FLOAT_VAR)
                           && left_instr->GetOperand2() == LOCL;
        bool is_right_var = (right_instr->GetType() == LOAD_INT_VAR || right_instr->GetType() == LOAD_FLOAT_VAR)
                            && right_instr->GetOperand2() == LOCL;

        if(is_left_var && is_right_var) {
          CSEKey key = { instr->GetType(), left_instr->GetOperand(), right_instr->GetOperand() };
          auto found = cse_map.find(key);
          if(found != cse_map.end()) {
            // CSE hit: replace with load of the previously stored result
            InstructionType load_type = (instr->GetType() >= ADD_FLOAT) ? LOAD_FLOAT_VAR : LOAD_INT_VAR;
            while(!working_stack.empty()) {
              outputs->AddInstruction(working_stack.back());
              working_stack.pop_back();
            }
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, load_type, found->second, LOCL));
          }
          else {
            // no CSE hit: emit normally
            while(!working_stack.empty()) {
              outputs->AddInstruction(working_stack.back());
              working_stack.pop_back();
            }
            outputs->AddInstruction(left_instr);
            outputs->AddInstruction(right_instr);
            outputs->AddInstruction(instr);

            // if the result gets stored to a local var immediately, track it
            if(i + 1 < input_instrs.size()) {
              IntermediateInstruction* next = input_instrs[i + 1];
              if((next->GetType() == STOR_INT_VAR || next->GetType() == STOR_FLOAT_VAR) && next->GetOperand2() == LOCL) {
                cse_map[key] = next->GetOperand();
              }
            }
          }
        }
        else {
          while(!working_stack.empty()) {
            outputs->AddInstruction(working_stack.back());
            working_stack.pop_back();
          }
          outputs->AddInstruction(left_instr);
          outputs->AddInstruction(right_instr);
          outputs->AddInstruction(instr);
        }
      }
      else {
        while(!working_stack.empty()) {
          outputs->AddInstruction(working_stack.back());
          working_stack.pop_back();
        }
        outputs->AddInstruction(instr);
      }
    }
      break;

    // invalidate CSE entries when a variable they reference is stored to
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      if(instr->GetOperand2() == LOCL) {
        for(auto it = cse_map.begin(); it != cse_map.end();) {
          if(it->first.left_slot == instr->GetOperand() ||
             it->first.right_slot == instr->GetOperand() ||
             it->second == instr->GetOperand()) {
            it = cse_map.erase(it);
          }
          else {
            ++it;
          }
        }
      }
      break;

    // invalidate everything on control flow / calls
    case MTHD_CALL:
    case DYN_MTHD_CALL:
    case JMP:
    case LBL:
    case RTRN:
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      cse_map.clear();
      break;

    default:
      while(!working_stack.empty()) {
        outputs->AddInstruction(working_stack.back());
        working_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }

  while(!working_stack.empty()) {
    outputs->AddInstruction(working_stack.back());
    working_stack.pop_back();
  }

  return outputs;
}

//
// ------------------- Dead Code Elimination -------------------
//
IntermediateBlock* ItermediateOptimizer::DeadCodeElim(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;

  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    // Pattern: JMP to immediately following LBL (redundant jump)
    if(instr->GetType() == JMP && i + 1 < input_instrs.size()) {
      IntermediateInstruction* next = input_instrs[i + 1];
      if(next->GetType() == LBL && instr->GetOperand() == next->GetOperand()) {
        // A conditional jump pops its condition, so dropping it alone left the
        // condition on the operand stack: `if(a < b) { v := v; }` lost its body
        // to RemoveUselessInstructions and then its JMP here, and the leftover
        // Bool became the caller's divisor ("Divide by zero") in the
        // interpreter. Keep the pop.
        if(instr->GetOperand2() >= 0) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(instr->GetStatement(), cur_line_num, POP_INT));
        }
        continue;
      }
    }

    outputs->AddInstruction(instr);
  }

  return outputs;
}

//
// ------------------- Peephole Optimization -------------------
//
IntermediateBlock* ItermediateOptimizer::PeepholeOptimize(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();

  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    // Pattern 1: LOAD_INT_LIT 0 + ADD_INT → remove both (x + 0 = x, 0 + x = x)
    if(instr->GetType() == ADD_INT && i >= 1) {
      IntermediateInstruction* prev = input_instrs[i - 1];
      if(prev->GetType() == LOAD_INT_LIT && prev->GetOperand7() == 0) {
        // remove the LOAD_INT_LIT 0 that was already added
        outputs->RemoveLastInstruction();
        continue;
      }
    }

    // Pattern 3: LOAD_INT_LIT 1 + MUL_INT → remove both (x * 1 = x, 1 * x = x)
    if(instr->GetType() == MUL_INT && i >= 1) {
      IntermediateInstruction* prev = input_instrs[i - 1];
      if(prev->GetType() == LOAD_INT_LIT && prev->GetOperand7() == 1) {
        outputs->RemoveLastInstruction();
        continue;
      }
    }

    // Pattern 4: LOAD_INT_LIT 0 + MUL_INT → replace with POP_INT + LOAD_INT_LIT 0
    // (x * 0 = 0, 0 * x = 0)
    if(instr->GetType() == MUL_INT && i >= 1) {
      IntermediateInstruction* prev = input_instrs[i - 1];
      if(prev->GetType() == LOAD_INT_LIT && prev->GetOperand7() == 0) {
        // remove the LOAD_INT_LIT 0
        outputs->RemoveLastInstruction();
        // pop the other operand, push 0
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, POP_INT));
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, (INT64_VALUE)0));
        continue;
      }
    }

    // There is no DIV_INT pattern. Division is not commutative, and the value
    // pushed last is the left operand, so `LOAD_INT_LIT 1; DIV_INT` is `1 / x`,
    // not `x / 1`. A pattern that removed both rewrote `1 / 5` to 5.

    outputs->AddInstruction(instr);
  }

  return outputs;
}

// ---- Dead Block Elimination ----
// After an unconditional JMP, instructions until the next reachable LBL are dead.
IntermediateBlock* ItermediateOptimizer::DeadBlockElimination(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();

  // Collect all reachable label IDs (JMP targets + TRY_START handler labels)
  std::unordered_set<long> jmp_targets;
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    const auto type = input_instrs[i]->GetType();
    if(type == JMP || type == TRY_START) {
      jmp_targets.insert(input_instrs[i]->GetOperand());
    }
    else if(type == JMP_TABLE) {
      jmp_targets.insert(input_instrs[i]->GetOperand3()); // default label
    }
    else if(type == JMP_TABLE_SLOT) {
      jmp_targets.insert(input_instrs[i]->GetOperand()); // case body label
    }
  }

  bool dead = false;
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    if(dead) {
      // A reachable label ends the dead region
      if(instr->GetType() == LBL && jmp_targets.find(instr->GetOperand()) != jmp_targets.end()) {
        dead = false;
        outputs->AddInstruction(instr);
      }
      // else: skip dead instruction
    }
    else {
      outputs->AddInstruction(instr);
      // After unconditional JMP, mark subsequent instructions as dead
      if(instr->GetType() == JMP && instr->GetOperand2() < 0) {
        dead = true;
      }
    }
  }

  return outputs;
}

//
// ------------------- Tail Call Optimization -------------------
//
// Calling convention recap:
//   Caller pushes: [arg0, arg1, ..., argN-1, instance]  (instance on TOP)
//   MTHD_CALL: pops instance, gives callee stack [arg0..argN-1]
//   Callee prologue: STOR slot[N-1], ..., STOR slot[0]  (pops all args)
//
// TCO approach: insert LBL tco_body AFTER the prologue STORs in the first block.
// At each tail-call site, replace MTHD_CALL+RTRN with:
//   POP_INT              (discard the new instance — same object, reuse frame's mem[0])
//   STOR slot[N-1] LOCL  (mirror prologue: store arg N-1)
//   ...
//   STOR slot[0]   LOCL
//   JMP tco_body         (jump past the prologue into the body)
//
std::vector<IntermediateBlock*> ItermediateOptimizer::TailCallOpt(std::vector<IntermediateBlock*> inputs)
{
  const std::wstring mthd_name = current_method->GetName();

  // skip: Main, constructors, and/or methods, virtual methods
  if(mthd_name.find(L":Main:") != std::wstring::npos ||
     mthd_name.find(L":New:") != std::wstring::npos ||
     current_method->HasAndOr() ||
     current_method->IsVirtual()) {
    return inputs;
  }

  // skip methods with exception handling — TCO would escape the try frame
  for(auto& block : inputs) {
    for(auto* instr : block->GetInstructions()) {
      if(instr->GetType() == TRY_START) {
        return inputs;
      }
    }
  }

  // collect the prologue STOR sequence from the first block
  // (initial unbroken run of STOR_*_VAR LOCL = param setup)
  std::vector<IntermediateInstruction*> prologue;
  if(!inputs.empty()) {
    for(auto* instr : inputs[0]->GetInstructions()) {
      if((instr->GetType() == STOR_INT_VAR || instr->GetType() == STOR_FLOAT_VAR ||
          instr->GetType() == STOR_FUNC_VAR) && instr->GetOperand2() == LOCL) {
        prologue.push_back(instr);
      }
      else {
        break;
      }
    }
  }

  const int self_class_id = current_method->GetClass()->GetId();
  const int self_method_id = current_method->GetId();

  // The body label must be new to THIS method, not just to the counter: an
  // inlined callee's labels are shifted by multiples of JUMP_OFF_INC and can
  // sit anywhere above it. When one coincided, a loop's backward jump resolved
  // to this label instead and re-ran the method from its top -- an infinite
  // loop at s3 in DecisionTreeClassifier:SortValues once lang gained three
  // functions and the counter moved (#1247).
  const int saved_unconditional_label = unconditional_label;
  const int max_label = MaxLabel(inputs, unconditional_label);
  const int tco_body_label = max_label + 1;
  unconditional_label = tco_body_label;
  bool found_tco = false;

  // first pass: rewrite tail calls
  std::vector<IntermediateBlock*> tco_outputs;
  for(auto* block : inputs) {
    IntermediateBlock* out_block = new IntermediateBlock;
    std::vector<IntermediateInstruction*> instrs = block->GetInstructions();

    for(size_t i = 0; i < instrs.size(); ++i) {
      IntermediateInstruction* instr = instrs[i];

      if(instr->GetType() == MTHD_CALL &&
         instr->GetOperand() == self_class_id &&
         instr->GetOperand2() == self_method_id &&
         i > 0 &&
         instrs[i - 1]->GetType() == LOAD_INST_MEM &&
         i + 1 < instrs.size() &&
         instrs[i + 1]->GetType() == RTRN) {
        // self tail call: the receiver (pushed last, so directly before the call) must be
        // @self -- a bare LOAD_INST_MEM. A call to a *different* instance of the same class
        // loads its receiver via LOAD_INT_VAR/etc., so it must NOT reuse the current frame.
        // tail call: pop the instance (top of stack), re-store args to locals, jump to body
        out_block->AddInstruction(
          IntermediateFactory::Instance()->MakeInstruction(cur_line_num, POP_INT));
        for(auto* stor : prologue) {
          out_block->AddInstruction(
            IntermediateFactory::Instance()->MakeInstruction(cur_line_num, stor->GetType(),
                                                             stor->GetOperand(), LOCL));
        }
        out_block->AddInstruction(
          IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, tco_body_label, -1));
        ++i; // consume the RTRN
        found_tco = true;
      }
      else {
        out_block->AddInstruction(instr);
      }
    }

    tco_outputs.push_back(out_block);
    delete block;
  }

  if(!found_tco) {
    unconditional_label = saved_unconditional_label; // reclaim unused label id
    return tco_outputs;
  }

  // second pass: insert tco_body label after the prologue STORs in the first block
  {
    std::vector<IntermediateInstruction*> existing = tco_outputs[0]->GetInstructions();
    IntermediateBlock* new_first = new IntermediateBlock;
    // copy prologue STORs. Index `prologue`, not `existing`: the loop bound comes
    // from prologue, and taking the elements from a different container only
    // happens to work because existing[0..prologue.size()) are the very same
    // instruction pointers -- prologue is the leading unbroken STOR run of
    // inputs[0], and the first pass copies anything before a tail call verbatim.
    // That is a real invariant, but an implicit one, and reading it out of the
    // other vector is an out-of-bounds read the moment it stops holding.
    for(size_t i = 0; i < prologue.size(); ++i) {
      new_first->AddInstruction(prologue[i]);
    }
    // insert the body label (TCO loops jump here)
    new_first->AddInstruction(
      IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LBL, tco_body_label, 0));
    // copy the rest of the body
    for(size_t i = prologue.size(); i < existing.size(); ++i) {
      new_first->AddInstruction(existing[i]);
    }
    delete tco_outputs[0];
    tco_outputs[0] = new_first;
  }

#ifdef _DEBUG
  GetLogger() << L"  TCO applied: " << current_method->GetName() << std::endl;
#endif

  return tco_outputs;
}

//
// ------------------- Loop-Invariant Code Motion (LICM) -------------------
//
// Built on the control-flow graph (cfg.h). A loop is a natural loop: a back edge to a
// block that dominates its source, so a `while`, a `for`, a `do`/`while` (whose back
// edge is a conditional jump) and a tail-call loop are all found the same way, and so
// is a loop with a `continue` (two back edges to one header). Code moves to the loop's
// preheader: the one block outside the loop that enters it.
//
// What moves, and when. A statement `t := <expr>` (a store to a local from a stack-
// balanced expression of local loads, literals and arithmetic that cannot trap) moves
// to the preheader when, in the unoptimized program:
//   1. nothing in the loop changes what it reads: each local it loads is not written in
//      the loop, or is written only by a statement that has already moved;
//   2. it is the only store to `t` in the loop;
//   3. `t` is not live at the loop's header -- no path from the header reads `t` before
//      the loop's own store writes it. That one test carries the rest of the usual
//      conditions (the statement's block dominates every use of `t` in the loop and every
//      exit where `t` is read afterwards, so a zero-trip loop, a store behind an `if` or a
//      `continue`, and a read before the store all keep `t` live and keep the store put);
//   4. no exception handler that a trap in or just before the loop can reach reads `t`
//      (a handler's reads count through the whole try region, so a loop inside a region
//      also keeps `t` live); and
//   5. it cannot trap or touch anything but locals, so running it where the loop might
//      not have is unobservable. Division is excluded for that reason (#1178).
// The pair `LOAD arr; LOAD_ARY_SIZE` of an array the loop never reassigns is replaced by
// a cached length. A null array traps there, so the pair only moves when it is at the top
// of the loop's header, ahead of anything the loop could do first: the trap then happens
// at the same point of the program, just before the first test instead of in it.
//
namespace {
  // an instruction that reads and writes nothing but the operand stack and local slots and
  // cannot trap: the only kind a statement may hold to move
  bool HoistableStackOp(InstructionType t, int& pops)
  {
    switch(t) {
    case ADD_INT:      case SUB_INT:      case MUL_INT:
    case BIT_AND_INT:  case BIT_OR_INT:   case BIT_XOR_INT:
    case SHL_INT:      case SHR_INT:
    case EQL_INT:      case NEQL_INT:     case LES_INT:      case GTR_INT:
    case LES_EQL_INT:  case GTR_EQL_INT:
    case AND_INT:      case OR_INT:
    case ADD_FLOAT:    case SUB_FLOAT:    case MUL_FLOAT:
    case EQL_FLOAT:    case NEQL_FLOAT:   case LES_FLOAT:    case GTR_FLOAT:
    case LES_EQL_FLOAT: case GTR_EQL_FLOAT:
      pops = 2;
      return true;

    case I2F:    case F2I:
    case BIT_NOT_INT:
      pops = 1;
      return true;

    default:
      return false;
    }
  }

  // an instruction that can run ahead of a null-array trap without anyone noticing
  bool QuietBeforeTrap(IntermediateInstruction* instr)
  {
    int pops = 0;
    switch(instr->GetType()) {
    case LBL:
    case LOAD_INT_LIT:
    case LOAD_CHAR_LIT:
    case LOAD_FLOAT_LIT:
      return true;

    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
      return instr->GetOperand2() == LOCL;

    default:
      return HoistableStackOp(instr->GetType(), pops);
    }
  }
}

bool ItermediateOptimizer::HoistFromLoop(const Cfg& cfg, size_t loop_index, std::vector<IntermediateInstruction*>& instrs)
{
  const Cfg::Loop& loop = cfg.Loops()[loop_index];
  if(loop.preheader < 0) {
    return false;
  }
  const std::vector<Cfg::Block>& blocks = cfg.Blocks();
  const Cfg::Block& header = blocks[loop.header];

  // how often each local is written in the loop
  std::map<long, int> defs;
  for(int b : loop.blocks) {
    for(size_t k = blocks[b].first; k <= blocks[b].last; ++k) {
      const Cfg::SlotUse use = Cfg::SlotUseOf(instrs[k]);
      if(use.write_a >= 0) {
        defs[use.write_a]++;
      }
      if(use.write_b >= 0) {
        defs[use.write_b]++;
      }
    }
  }

  // the handlers a trap around the loop can reach, outside it: a store hoisted ahead of the
  // loop must not be visible to them
  std::vector<int> outside_handlers = blocks[loop.preheader].handlers;
  for(const Cfg::Exit& e : loop.exits) {
    if(e.is_handler) {
      outside_handlers.push_back(e.to);
    }
  }
  auto read_by_handler = [&](long slot) {
    for(int h : outside_handlers) {
      if(cfg.LiveIn(h, slot)) {
        return true;
      }
    }
    return false;
  };

  std::vector<bool> removed(instrs.size(), false);
  std::map<size_t, IntermediateInstruction*> replaced; // an instruction swapped for another
  std::vector<IntermediateInstruction*> hoisted;

  // allocate a fresh LOCL int slot and update method metadata; -1 when the
  // slot would take the method past LOCAL_SIZE, the local space an
  // interpreter frame holds (EmitMethod checked the unoptimized method)
  auto alloc_int_slot = [this]() -> int {
    if(current_method->GetSpace() + (int)sizeof(INT64_VALUE) > LOCAL_SIZE) {
      return -1;
    }
    int slot = current_method->GetEntries()->GetSlotCount() +
               (current_method->HasAndOr() ? 1 : 0);
    current_method->GetEntries()->AddParameter(new IntermediateDeclaration(L"", INT_PARM));
    current_method->SetSpace(current_method->GetSpace() + (int)sizeof(INT64_VALUE));
    return slot;
  };

  // === Pattern 1: LOAD_INT_VAR arr LOCL + LOAD_ARY_SIZE ===
  // The length of an array the loop never reassigns, read at the top of the header.
  std::map<long, int> arr_to_tmp;
  for(size_t k = header.first; k <= header.last; ++k) {
    IntermediateInstruction* instr = instrs[k];
    if(instr->GetType() == LOAD_INT_VAR && instr->GetOperand2() == LOCL && k + 1 <= header.last &&
       instrs[k + 1]->GetType() == LOAD_ARY_SIZE) {
      const long arr = instr->GetOperand();
      if(defs.count(arr)) {
        break; // this array changes in the loop: its length read stays, and may be the first trap
      }
      if(!arr_to_tmp.count(arr)) {
        const int tmp = alloc_int_slot();
        if(tmp < 0) {
          break;
        }
        arr_to_tmp[arr] = tmp;
        hoisted.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, arr, LOCL));
        hoisted.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_ARY_SIZE));
        hoisted.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR, tmp, LOCL));
      }
      ++k; // the length read moves, in order, so what follows it is still ahead of any trap
      continue;
    }
    if(!QuietBeforeTrap(instr)) {
      break;
    }
  }
  if(!arr_to_tmp.empty()) {
    // every read of a cached length in the loop becomes a load of the cache
    for(int b : loop.blocks) {
      for(size_t k = blocks[b].first; k + 1 <= blocks[b].last; ++k) {
        IntermediateInstruction* instr = instrs[k];
        if(instr->GetType() == LOAD_INT_VAR && instr->GetOperand2() == LOCL && instrs[k + 1]->GetType() == LOAD_ARY_SIZE) {
          auto it = arr_to_tmp.find(instr->GetOperand());
          if(it != arr_to_tmp.end()) {
            replaced[k] = IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, it->second, LOCL);
            removed[k + 1] = true;
            ++k;
          }
        }
      }
    }
  }

  // === Pattern 2: loop-invariant statements ===
  std::set<long> moved_slots; // the slots whose only store in the loop has moved
  for(int b : loop.blocks) {
    int depth = 0;
    size_t start = 0;
    std::set<long> loads;
    bool open = false;
    auto reset = [&]() {
      depth = 0;
      loads.clear();
      open = false;
    };

    for(size_t k = blocks[b].first; k <= blocks[b].last; ++k) {
      IntermediateInstruction* instr = instrs[k];
      const InstructionType t = instr->GetType();
      int pops = 0;

      if(removed[k] || replaced.count(k)) {
        reset();
        continue;
      }

      if(t == LOAD_INT_LIT || t == LOAD_CHAR_LIT || t == LOAD_FLOAT_LIT) {
        if(!open) { open = true; start = k; }
        ++depth;
      }
      else if((t == LOAD_INT_VAR || t == LOAD_FLOAT_VAR) && instr->GetOperand2() == LOCL) {
        if(!open) { open = true; start = k; }
        loads.insert(instr->GetOperand());
        ++depth;
      }
      else if(open && HoistableStackOp(t, pops)) {
        if(depth < pops) {
          reset();
          continue;
        }
        depth -= pops - 1;
      }
      else if((t == STOR_INT_VAR || t == STOR_FLOAT_VAR) && instr->GetOperand2() == LOCL && open && depth == 1) {
        const long slot = instr->GetOperand();
        bool ok = defs[slot] == 1 && !cfg.LiveIn(loop.header, slot) && !read_by_handler(slot);
        for(auto it = loads.begin(); ok && it != loads.end(); ++it) {
          ok = !defs.count(*it) || moved_slots.count(*it);
        }
        if(ok) {
          for(size_t j = start; j <= k; ++j) {
            hoisted.push_back(instrs[j]);
            removed[j] = true;
          }
          moved_slots.insert(slot);
        }
        reset();
      }
      else {
        reset();
      }
    }
  }

  if(hoisted.empty()) {
    return false;
  }

  std::vector<IntermediateInstruction*> result;
  result.reserve(instrs.size() + hoisted.size());
  for(size_t k = 0; k < instrs.size(); ++k) {
    if(k == loop.preheader_at) {
      result.insert(result.end(), hoisted.begin(), hoisted.end());
    }
    if(removed[k]) {
      continue;
    }
    auto swap = replaced.find(k);
    result.push_back(swap != replaced.end() ? swap->second : instrs[k]);
  }
  instrs = std::move(result);
  return true;
}

IntermediateBlock* ItermediateOptimizer::LICM(IntermediateBlock* input)
{
  // RunPass deletes the input block after we return — never return input or delete it.
  std::vector<IntermediateInstruction*> instrs = input->GetInstructions();

  // One loop at a time, innermost first. A move changes the list, so the graph is built
  // again before the next loop: a statement that moved out of an inner loop now sits in
  // the outer loop's body and may move again.
  std::set<long> tried;
  for(int round = 0; round < 512; ++round) {
    Cfg cfg(instrs);
    // an irreducible edge means some cycle is not a natural loop, so a loop's block set
    // may miss writes that happen on it: leave such a method alone
    if(!cfg.WellFormed() || cfg.Loops().empty() || cfg.IrreducibleEdges() != 0) {
      break;
    }
    cfg.ComputeLiveness();

    bool changed = false;
    for(size_t i = 0; i < cfg.Loops().size() && !changed; ++i) {
      IntermediateInstruction* head = instrs[cfg.Blocks()[cfg.Loops()[i].header].first];
      if(head->GetType() != LBL || !tried.insert(head->GetOperand()).second) {
        continue;
      }
      changed = HoistFromLoop(cfg, i, instrs);
    }

    if(!changed) {
      break;
    }
  }

  // always return a new block — RunPass owns and deletes the input
  IntermediateBlock* licm_output = new IntermediateBlock;
  for(IntermediateInstruction* instr : instrs) {
    licm_output->AddInstruction(instr);
  }

  return licm_output;
}
