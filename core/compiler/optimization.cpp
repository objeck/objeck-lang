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
#include <functional>
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
    else if(o == L"s3") {
      optimization_level = 3;
    }
    else {
      optimization_level = 3;
    }
  }

  jump_inline_offset = 0;
}

void ItermediateOptimizer::Optimize()
{
#ifdef _DEBUG
  GetLogger() << L"\n--------- Optimizing Code ---------" << std::endl;
#endif

  // classes...
  std::vector<IntermediateClass*> klasses = program->GetClasses();
  for(size_t i = 0; i < klasses.size(); ++i) {
    // methods...
    std::vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
    for(size_t j = 0; j < methods.size(); ++j) {
      current_method = methods[j];
#ifdef _DEBUG
      GetLogger() << L"Optimizing method, pass 1: name='" << current_method->GetName() << "'" << std::endl;
#endif
      current_method->SetBlocks(OptimizeMethod(current_method->GetBlocks()));
    }
    
    if(!is_lib) {
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
        working_stack.pop_front();
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
      IntermediateMethod* mthd_called = program->GetClass(static_cast<int>(instr->GetOperand()))->GetMethod(static_cast<int>(instr->GetOperand2()));
      int status = CanInlineSetterGetter(mthd_called);
      //  getter instance pattern
      if(status == 0) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(instrs[1]);
      }
      // getter instance pattern
      else if(status == 1) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, POP_INT));
        outputs->AddInstruction(instrs[0]);
      }
      // character print pattern
      else if(status == 2) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, POP_INT));
        outputs->AddInstruction(instrs[2]);
        outputs->AddInstruction(instrs[3]);
      }
      else if(status == 3) {
        std::vector<IntermediateBlock*> blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> instrs = blocks[0]->GetInstructions();
        outputs->AddInstruction(instrs[3]);
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

bool ItermediateOptimizer::CanInlineMethod(IntermediateMethod* mthd_called, std::set<IntermediateMethod*>& inlined_mthds, std::set<int>& lbl_jmp_offsets)
{
  // don't inline the same method more then once, since you'll have label/jump conflicts
  std::set<IntermediateMethod*>::iterator found = inlined_mthds.find(mthd_called);
  if(found != inlined_mthds.end()) {
    jump_inline_offset += JUMP_OFF_INC;
  }

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

  // don't inline into "main" since it's not JIT compiled
  const std::wstring curr_mthd_name = current_method->GetName();
  if(curr_mthd_name.find(L":Main:o.System.String*,") != std::wstring::npos) {
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

  return true;
}

int ItermediateOptimizer::CanInlineSetterGetter(IntermediateMethod* mthd_called)
{
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

IntermediateBlock* ItermediateOptimizer::StrengthReduction(IntermediateBlock* inputs)
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

    case LOAD_INT_VAR:
      working_stack.push_front(instr);
      break;

    case MUL_INT:
    case DIV_INT:
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
    // shift left or right
    if(instr->GetType() == MUL_INT) {
      rewrite_instrs.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, SHL_INT, (long)shift));
    }
    else {
      rewrite_instrs.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, SHR_INT, (long)shift));
    }
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
      IntermediateMethod* mthd_called = program->GetClass(static_cast<int>(instr->GetOperand()))->GetMethod(static_cast<int>(instr->GetOperand2()));
      // checked called method to determine if it can be inlined
      if(CanInlineMethod(mthd_called, inlined_mthds, lbl_jmp_offsets)) {
        // calculate offset
        IntermediateDeclarations* current_entries = current_method->GetEntries();
        int local_instr_offset = 1;
        std::vector<IntermediateDeclaration*> current_dclrs = current_entries->GetParameters();
        for(size_t j = 0; j < current_dclrs.size(); ++j) {
          local_instr_offset++;
        }

        if(current_method->HasAndOr() || mthd_called->HasAndOr()) {
          local_instr_offset++;
        }

        // adjust local space
        current_method->SetSpace(current_method->GetSpace() + sizeof(INT_VALUE) * 2 + mthd_called->GetSpace());

        // fetch inline instructions for called method
        std::vector<IntermediateBlock*> mthd_called_blocks = mthd_called->GetBlocks();
        std::vector<IntermediateInstruction*> mthd_called_instrs = mthd_called_blocks[0]->GetInstructions();

        // handle the storing of local instance
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR,
          local_instr_offset - 1, LOCL));

        current_entries->AddParameter(new IntermediateDeclaration(L"", OBJ_PARM));
        if(mthd_called->HasAndOr()) {
          current_entries->AddParameter(new IntermediateDeclaration(L"", INT_PARM));
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
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, mthd_called_instr->GetOperand() + jump_inline_offset,
                                                                                     mthd_called_instr->GetOperand2()));
            break;

          case LBL:
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
  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case LBL:
      lbl_offsets.insert(std::pair<int, int>(instr->GetOperand(), (int)i + 1));
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
#ifdef _DEBUG
      assert(result != lbl_offsets.end());
#endif
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, result->second, instr->GetOperand2()));
    }
      break;

    case TRY_START: {
      std::unordered_map<int, int>::iterator result = lbl_offsets.find(static_cast<int>(instr->GetOperand()));
#ifdef _DEBUG
      assert(result != lbl_offsets.end());
#endif
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, TRY_START, result->second, instr->GetOperand2()));
    }
      break;

    case JMP_TABLE: {
      std::unordered_map<int, int>::iterator def_result = lbl_offsets.find((int)instr->GetOperand3());
#ifdef _DEBUG
      assert(def_result != lbl_offsets.end());
#endif
      const long default_ip = (def_result != lbl_offsets.end()) ? (long)def_result->second : 0;
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP_TABLE,
        instr->GetOperand(), instr->GetOperand2(), default_ip));
    }
      break;

    case JMP_TABLE_SLOT: {
      std::unordered_map<int, int>::iterator slot_result = lbl_offsets.find((int)instr->GetOperand());
#ifdef _DEBUG
      assert(slot_result != lbl_offsets.end());
#endif
      const long target_ip = (slot_result != lbl_offsets.end()) ? (long)slot_result->second : 0;
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

  std::vector<std::pair<size_t, size_t>> dead_store_edits;

  for(size_t i = start_pos; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
      if(instr->GetStatement() && IsDeadStore(instr, i, input_instrs)) {
        const std::pair<size_t, size_t> dead_store_edit = DeadStoreEdit(i, input_instrs);
        dead_store_edits.push_back(dead_store_edit);
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
    for(size_t i = 0; i < input_instrs.size(); ++i) {
      if(!InDeadStoreRange(i, dead_store_edits)) {
        outputs->AddInstruction(input_instrs[i]);
      }
    }
  }

  return outputs;
}

bool ItermediateOptimizer::InDeadStoreRange(size_t pos, std::vector<std::pair<size_t, size_t>> dead_store_edits)
{
  for(size_t i = 0; i < dead_store_edits.size(); ++i) {
    const std::pair<size_t, size_t> dead_store_edit = dead_store_edits[i];
    if(pos >= dead_store_edit.first && pos <= dead_store_edit.second) {
      return true;
    }
  }

  return false;
}
std::pair<size_t, size_t> ItermediateOptimizer::DeadStoreEdit(size_t start_pos, std::vector<IntermediateInstruction*>& input_instrs)
{
  size_t end_pos = start_pos;
  IntermediateInstruction* dead_store_instr = input_instrs[start_pos];

  bool done = false;
  for(std::vector<IntermediateInstruction*>::iterator start_iter = input_instrs.begin() + start_pos - 1;
      !done && start_iter != input_instrs.begin(); --start_iter) {
    if((*start_iter)->GetStatement() == dead_store_instr->GetStatement()) {
      --end_pos;
    }
    else {
      done = true;
    }
  }

  return std::pair<size_t, size_t>(end_pos, start_pos);
}

bool ItermediateOptimizer::IsDeadStore(IntermediateInstruction* check_instr, size_t check_pos, std::vector<IntermediateInstruction*>& input_instrs)
{
  for(size_t i = check_pos + 1; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case JMP:
    case LBL:
    case RTRN:
      return false;

    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
      if(instr->GetOperand() == check_instr->GetOperand() && instr->GetOperand2() == LOCL && check_instr->GetOperand2() == LOCL) {
        return false;
      }
      break;

    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
      if(instr->GetOperand() == check_instr->GetOperand() && instr->GetOperand2() == LOCL && check_instr->GetOperand2() == LOCL) {
        return true;
      }
      break;
     
    default:
      break;
    }
  }

  return false;
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
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, result->second.int_value));
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
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, result->second.float_value));
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
    case ADD_INT: {
      const INT64_VALUE value = left->GetOperand7() + right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case SUB_INT: {
      const INT64_VALUE value = left->GetOperand7() - right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case MUL_INT: {
      const INT64_VALUE value = left->GetOperand7() * right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case DIV_INT: {
      if(right->GetOperand7() == 0) {
        working_stack.push_front(right);
        working_stack.push_front(left);
        outputs->AddInstruction(instr);
        return;
      }
      const INT64_VALUE value = left->GetOperand7() / right->GetOperand7();
      working_stack.push_front(IntermediateFactory::Instance()->MakeIntLitInstruction(cur_line_num, value));
    }
      break;

    case MOD_INT: {
      if(right->GetOperand7() == 0) {
        working_stack.push_front(right);
        working_stack.push_front(left);
        outputs->AddInstruction(instr);
        return;
      }
      const INT64_VALUE value = left->GetOperand7() % right->GetOperand7();
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

    // Pattern 5: LOAD_INT_LIT 1 + DIV_INT → remove both (x / 1 = x)
    if(instr->GetType() == DIV_INT && i >= 1) {
      IntermediateInstruction* prev = input_instrs[i - 1];
      if(prev->GetType() == LOAD_INT_LIT && prev->GetOperand7() == 1) {
        outputs->RemoveLastInstruction();
        continue;
      }
    }

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
  const int tco_body_label = ++unconditional_label;
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
         i + 1 < instrs.size() &&
         instrs[i + 1]->GetType() == RTRN) {
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
    --unconditional_label; // reclaim unused label id
    return tco_outputs;
  }

  // second pass: insert tco_body label after the prologue STORs in the first block
  {
    std::vector<IntermediateInstruction*> existing = tco_outputs[0]->GetInstructions();
    IntermediateBlock* new_first = new IntermediateBlock;
    // copy prologue STORs
    for(size_t i = 0; i < prologue.size(); ++i) {
      new_first->AddInstruction(existing[i]);
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
IntermediateBlock* ItermediateOptimizer::LICM(IntermediateBlock* input)
{
  // RunPass deletes the input block after we return — never return input or delete it.
  std::vector<IntermediateInstruction*> instrs = input->GetInstructions();

  // build label-id → position map for back-edge detection
  std::unordered_map<int, size_t> lbl_pos;
  for(size_t i = 0; i < instrs.size(); ++i) {
    if(instrs[i]->GetType() == LBL) {
      lbl_pos[(int)instrs[i]->GetOperand()] = i;
    }
  }

  // find natural loop back-edges: unconditional JMP targeting an earlier label
  struct Loop { size_t header; size_t backedge; };
  std::vector<Loop> loops;
  for(size_t i = 0; i < instrs.size(); ++i) {
    if(instrs[i]->GetType() == JMP && instrs[i]->GetOperand2() < 0) {
      auto it = lbl_pos.find((int)instrs[i]->GetOperand());
      if(it != lbl_pos.end() && it->second < i) {
        loops.push_back({it->second, i});
      }
    }
  }

  // allocate a fresh LOCL int slot and update method metadata
  auto alloc_int_slot = [this]() -> int {
    int slot = (int)current_method->GetEntries()->GetParameters().size() +
               (current_method->HasAndOr() ? 1 : 0);
    current_method->GetEntries()->AddParameter(new IntermediateDeclaration(L"", INT_PARM));
    current_method->SetSpace(current_method->GetSpace() + (int)sizeof(INT64_VALUE));
    return slot;
  };

  std::set<size_t> skip_pos;
  std::map<size_t, std::vector<IntermediateInstruction*>> insert_before;

  for(auto& loop : loops) {
    const size_t lo = loop.header;
    const size_t hi = loop.backedge;

    // collect write counts for every LOCL slot written inside this loop
    std::map<int, int> store_counts;
    for(size_t i = lo; i <= hi; ++i) {
      if(skip_pos.count(i)) continue;
      auto* instr = instrs[i];
      if(instr->GetOperand2() == LOCL) {
        switch(instr->GetType()) {
        case STOR_INT_VAR: case STOR_FLOAT_VAR: case STOR_FUNC_VAR:
          store_counts[static_cast<int>(instr->GetOperand())]++;
          break;
        default:
          break;
        }
      }
    }

    // collect LOCL slots written anywhere OUTSIDE this loop in the same block
    std::set<int> outside_store_slots;
    for(size_t i = 0; i < instrs.size(); ++i) {
      if(i >= lo && i <= hi) continue;
      if(skip_pos.count(i)) continue;
      auto* instr = instrs[i];
      if(instr->GetOperand2() == LOCL) {
        switch(instr->GetType()) {
        case STOR_INT_VAR: case STOR_FLOAT_VAR: case STOR_FUNC_VAR:
          outside_store_slots.insert(static_cast<int>(instr->GetOperand()));
          break;
        default:
          break;
        }
      }
    }

    // === Pattern 1: LOAD_INT_VAR arr LOCL + LOAD_ARY_SIZE ===
    // Hoist array-size reads when the array variable is loop-invariant.
    std::map<int, int> arr_to_tmp; // arr_slot → tmp_slot (reuse per unique arr)
    for(size_t i = lo; i + 1 <= hi; ++i) {
      if(skip_pos.count(i) || skip_pos.count(i + 1)) continue;
      auto* instr = instrs[i];
      auto* next  = instrs[i + 1];

      if(instr->GetType() == LOAD_INT_VAR && instr->GetOperand2() == LOCL &&
         next->GetType()  == LOAD_ARY_SIZE &&
         !store_counts.count(static_cast<int>(instr->GetOperand()))) {
        const int arr_slot = static_cast<int>(instr->GetOperand());

        int tmp_slot;
        auto existing = arr_to_tmp.find(arr_slot);
        if(existing != arr_to_tmp.end()) {
          tmp_slot = existing->second;
        }
        else {
          tmp_slot = alloc_int_slot();
          arr_to_tmp[arr_slot] = tmp_slot;
          // emit: LOAD arr, LOAD_ARY_SIZE, STOR tmp — before the loop header
          auto& pre = insert_before[lo];
          pre.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR,  arr_slot, LOCL));
          pre.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_ARY_SIZE));
          pre.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR,  tmp_slot, LOCL));
        }

        // replace the in-loop pair with a single load from the cached slot
        skip_pos.insert(i);
        skip_pos.insert(i + 1);
        insert_before[i].push_back(
          IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, tmp_slot, LOCL));
        ++i; // skip LOAD_ARY_SIZE
      }
    }

    // === Pattern 2: Loop-invariant arithmetic statements ===
    // Detect stack-balanced pure-LOCL arithmetic ending in STOR_*_VAR.
    int stack_depth = 0;
    std::vector<size_t> stmt_idx;
    bool stmt_safe = true;
    std::set<int> stmt_loads; // LOCL slots read in this statement

    auto reset_stmt = [&](size_t /* next */) {
      stack_depth = 0;
      stmt_idx.clear();
      stmt_safe = true;
      stmt_loads.clear();
    };

    for(size_t i = lo; i <= hi; ++i) {
      if(skip_pos.count(i)) { reset_stmt(i + 1); continue; }
      auto* instr = instrs[i];
      InstructionType t = instr->GetType();

      switch(t) {
      case LOAD_INT_LIT:
      case LOAD_CHAR_LIT:
        stack_depth++;
        stmt_idx.push_back(i);
        break;

      case LOAD_FLOAT_LIT:
        stack_depth++;
        stmt_idx.push_back(i);
        break;

      case LOAD_INT_VAR:
        if(instr->GetOperand2() != LOCL) { stmt_safe = false; }
        else { stmt_loads.insert(static_cast<int>(instr->GetOperand())); }
        stack_depth++;
        stmt_idx.push_back(i);
        break;

      case LOAD_FLOAT_VAR:
        if(instr->GetOperand2() != LOCL) { stmt_safe = false; }
        else { stmt_loads.insert(static_cast<int>(instr->GetOperand())); }
        stack_depth++;
        stmt_idx.push_back(i);
        break;

      // binary ops (pop 2, push 1)
      case ADD_INT:      case SUB_INT:      case MUL_INT:
      case BIT_AND_INT:  case BIT_OR_INT:   case BIT_XOR_INT:
      case SHL_INT:      case SHR_INT:
      case EQL_INT:      case NEQL_INT:     case LES_INT:      case GTR_INT:
      case LES_EQL_INT:  case GTR_EQL_INT:
      case AND_INT:      case OR_INT:
        if(stack_depth < 2) { reset_stmt(i + 1); break; }
        stack_depth--;
        stmt_idx.push_back(i);
        break;

      // DIV_INT / MOD_INT can TRAP (divide by zero). Track the stack like other
      // binary ops but mark the statement unsafe so it is never hoisted out of a
      // loop that may execute zero times — doing so would turn a never-evaluated
      // division into an always-evaluated div-by-zero. (Float division yields
      // inf/nan instead of trapping, so it stays hoistable above.)
      case DIV_INT:      case MOD_INT:
        if(stack_depth < 2) { reset_stmt(i + 1); break; }
        stack_depth--;
        stmt_idx.push_back(i);
        stmt_safe = false;
        break;

      case ADD_FLOAT:    case SUB_FLOAT:    case MUL_FLOAT:    case DIV_FLOAT:
      case EQL_FLOAT:    case NEQL_FLOAT:   case LES_FLOAT:    case GTR_FLOAT:
      case LES_EQL_FLOAT: case GTR_EQL_FLOAT:
        if(stack_depth < 2) { reset_stmt(i + 1); break; }
        stack_depth--;
        stmt_idx.push_back(i);
        break;

      // conversion / unary ops (pop 1, push 1)
      case I2F:    case F2I:
      case BIT_NOT_INT:
        if(stack_depth < 1) { reset_stmt(i + 1); break; }
        stmt_idx.push_back(i);
        break;

      // statement end: STOR with depth 1 → LOCL slot
      case STOR_INT_VAR:
      case STOR_FLOAT_VAR: {
        if(instr->GetOperand2() == LOCL && stack_depth == 1 &&
           stmt_safe && !stmt_idx.empty()) {
          const int stor_slot = static_cast<int>(instr->GetOperand());

          bool can_hoist =
            store_counts.count(stor_slot) && store_counts[stor_slot] == 1 &&
            !outside_store_slots.count(stor_slot);

          if(can_hoist) {
            for(int s : stmt_loads) {
              if(store_counts.count(s) && store_counts[s] > 0) {
                can_hoist = false;
                break;
              }
            }
          }

          if(can_hoist) {
            auto& pre = insert_before[lo];
            for(size_t idx : stmt_idx) {
              pre.push_back(instrs[idx]);
              skip_pos.insert(idx);
            }
            pre.push_back(instr);
            skip_pos.insert(i);
          }
        }
        reset_stmt(i + 1);
        break;
      }

      default:
        reset_stmt(i + 1);
        break;
      }
    }
  }

  // build output block: inject hoisted instructions, omit hoisted originals
  // always return a new block — RunPass owns and deletes the input
  IntermediateBlock* licm_output = new IntermediateBlock;
  for(size_t i = 0; i < instrs.size(); ++i) {
    auto it = insert_before.find(i);
    if(it != insert_before.end()) {
      for(auto* h : it->second) {
        licm_output->AddInstruction(h);
      }
    }
    if(!skip_pos.count(i)) {
      licm_output->AddInstruction(instrs[i]);
    }
  }

  return licm_output;
}

