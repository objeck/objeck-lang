/***************************************************************************
 * Platform independent language optimizer.
 *
 * Copyright (c) 2023, Randy Hollines
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

  // primitive 'Float
  can_inline.insert(L"System.$Float:Size:f*,");
  can_inline.insert(L"System.$Float:Sin:f,");
  can_inline.insert(L"System.$Float:Cos:f,");
  can_inline.insert(L"System.$Float:Tan:f,");
  can_inline.insert(L"System.$Float:SquareRoot:f,");
  can_inline.insert(L"System.$Float:Log:f,");
  can_inline.insert(L"System.$Float:Ceiling:f,");
  can_inline.insert(L"System.$Float:Floor:f,");
  can_inline.insert(L"System.$Float:ArcSin:f,");
  can_inline.insert(L"System.$Float:ArcCos:f,");
  can_inline.insert(L"System.$Float:Abs:f,");
  can_inline.insert(L"System.$Float:ArcTan2:f,f,");
  can_inline.insert(L"System.$Float:Power:f,f,");
  can_inline.insert(L"System.$Float:Max:f,f,");
  can_inline.insert(L"System.$Float:Min:f,f,");
  can_inline.insert(L"System.$Float:Pi:");
  can_inline.insert(L"System.$Float:E:");
  // primitive 'Int'
  can_inline.insert(L"System.$Int:Size:i*,");
  can_inline.insert(L"System.$Int:Max:i,i,");
  can_inline.insert(L"System.$Int:Min:i,i,");
  can_inline.insert(L"System.$Int:Factorial:i,");
  can_inline.insert(L"System.$Int:Abs:i,");
  // primitive 'Char'
  can_inline.insert(L"System.$Char:Size:c*,");
  can_inline.insert(L"System.$Char:Max:c,c,");
  can_inline.insert(L"System.$Char:Min:c,c,");
  can_inline.insert(L"System.$Char:Abs:c,");
  // primitive 'Byte'
  can_inline.insert(L"System.$Byte:Size:b*,");
  can_inline.insert(L"System.$Byte:Max:b,b,");
  can_inline.insert(L"System.$Byte:Min:b,b,");
  can_inline.insert(L"System.$Byte:Abs:b,");
  // built-in types
  can_inline.insert(L"System.$BaseArray:Size:o.System.Base*,");
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

  if(!is_lib) {
    std::vector<IntermediateClass*> klasses = program->GetClasses();
    for(size_t i = 0; i < klasses.size(); ++i) {
      std::vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
      for(size_t j = 0; j < methods.size(); ++j) {
        current_method = methods[j];
#ifdef _DEBUG
        GetLogger() << L"Optimizing jumps, pass 2: name='" << current_method->GetName() << "'" << std::endl;
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

std::vector<IntermediateBlock*> ItermediateOptimizer::JumpToLocation(std::vector<IntermediateBlock*> inputs)
{
#ifdef _DEBUG
  GetLogger() << L"  Method inlining..." << std::endl;
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
  // clean up jump addresses
#ifdef _DEBUG
  GetLogger() << L"  Clean up jumps..." << std::endl;
#endif

  std::vector<IntermediateBlock*> jump_blocks;
  // clean up jumps
  while(!inputs.empty()) {
    IntermediateBlock* tmp = inputs.front();
    jump_blocks.push_back(CleanJumps(tmp));
    // delete old block
    inputs.erase(inputs.begin());
    delete tmp;
    tmp = nullptr;
  }
  
  std::vector<IntermediateBlock*> useless_instrs_blocks;
  // remove useless instructions
#ifdef _DEBUG
  GetLogger() << L"  Clean up Instructions..." << std::endl;
#endif
  while(!jump_blocks.empty()) {
    IntermediateBlock* tmp = jump_blocks.front();
    useless_instrs_blocks.push_back(RemoveUselessInstructions(tmp));
    // delete old block
    jump_blocks.erase(jump_blocks.begin());
    delete tmp;
    tmp = nullptr;
  }
  
  std::vector<IntermediateBlock*> folded_float_blocks;
  if(!is_lib && optimization_level > 0) {
    std::vector<IntermediateBlock*> getter_setter_blocks;
    // getter/setter inlining
#ifdef _DEBUG
    GetLogger() << L"  Getter/setter inlining..." << std::endl;
#endif
    while(!useless_instrs_blocks.empty()) {
      IntermediateBlock* tmp = useless_instrs_blocks.front();
      getter_setter_blocks.push_back(InlineSettersGetters(tmp));
      // delete old block
      useless_instrs_blocks.erase(useless_instrs_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }

    // dead store
#ifdef _DEBUG
    GetLogger() << L"  Dead store..." << std::endl;
#endif
    std::vector<IntermediateBlock*> dead_store_blocks;
    while(!getter_setter_blocks.empty()) {
      IntermediateBlock* tmp = getter_setter_blocks.front();
      dead_store_blocks.push_back(DeadStore(tmp));
      // delete old block
      getter_setter_blocks.erase(getter_setter_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }

    // constant propagation
#ifdef _DEBUG
    GetLogger() << L"  Constant propagation..." << std::endl;
#endif
    std::vector<IntermediateBlock*> const_prop_blocks;
    while(!dead_store_blocks.empty()) {
      IntermediateBlock* tmp = dead_store_blocks.front();
      const_prop_blocks.push_back(ConstantProp(tmp));
      // delete old block
      dead_store_blocks.erase(dead_store_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }

    // fold integers
#ifdef _DEBUG
    GetLogger() << L"  Folding integers..." << std::endl;
#endif
    std::vector<IntermediateBlock*> folded_int_blocks;
    while(!const_prop_blocks.empty()) {
      IntermediateBlock* tmp = const_prop_blocks.front();
      folded_int_blocks.push_back(FoldIntConstants(tmp));
      // delete old block
      const_prop_blocks.erase(const_prop_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }
    
    // fold floats
#ifdef _DEBUG
    GetLogger() << L"  Folding floats..." << std::endl;
#endif
    while(!folded_int_blocks.empty()) {
      IntermediateBlock* tmp = folded_int_blocks.front();
      folded_float_blocks.push_back(FoldFloatConstants(tmp));
      // delete old block
      folded_int_blocks.erase(folded_int_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }
  } 
  else {
    return useless_instrs_blocks;
  }

  std::vector<IntermediateBlock*> strength_reduced_blocks;
  if(optimization_level > 1) {
    // reduce strength
#ifdef _DEBUG
    GetLogger() << L"  Strength reduction..." << std::endl;
#endif
    while(!folded_float_blocks.empty()) {
      IntermediateBlock* tmp = folded_float_blocks.front();
      strength_reduced_blocks.push_back(StrengthReduction(tmp));
      // delete old block
      folded_float_blocks.erase(folded_float_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }
  } 
  else {
    return folded_float_blocks;
  }

  std::vector<IntermediateBlock*> instruction_replaced_blocks;
  if(optimization_level > 2) {
    // instruction replacement
#ifdef _DEBUG
    GetLogger() << L"  Instruction replacement..." << std::endl;
#endif
    while(!strength_reduced_blocks.empty()) {
      IntermediateBlock* tmp = strength_reduced_blocks.front();
      instruction_replaced_blocks.push_back(InstructionReplacement(tmp));
      // delete old block
      strength_reduced_blocks.erase(strength_reduced_blocks.begin());
      delete tmp;
      tmp = nullptr;
    }
  } 
  else {
    return strength_reduced_blocks;
  }
  
  return instruction_replaced_blocks;
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
      IntermediateMethod* mthd_called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
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
      if(!working_stack.empty() && (working_stack.front()->GetType() == STOR_INT_VAR && working_stack.front()->GetType() == STOR_FLOAT_VAR)) {
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
    return false;
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
  if(cls_name_str.find(L'$') != std::wstring::npos) {
    std::set<std::wstring>::iterator result = can_inline.find(mthd_called->GetName());
    if(result == can_inline.end()) {
      return false;
    };
  }

  if(current_method->GetSpace() + mthd_called->GetSpace() > LOCL_INLINE_MEM_MAX) {
    return false;
  }

  // ignore constructors
  const std::wstring called_mthd_name = mthd_called->GetName();
  if(called_mthd_name.find(L":New:") != std::wstring::npos) {
    return false;
  }

  // don't inline into "main" since it's not JTI compiled
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
    case instructions::DLL_LOAD:
    case instructions::DLL_UNLOAD:
    case instructions::DLL_FUNC_CALL:
    case instructions::THREAD_JOIN:
    case instructions::THREAD_SLEEP:
    case instructions::THREAD_MUTEX:
    case instructions::CRITICAL_START:
    case instructions::CRITICAL_END:
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

      // look for conflicting jump offsets
    case instructions::LBL:
    case instructions::JMP:
      if(lbl_jmp_offsets.find(mthd_called_instr->GetOperand()) != lbl_jmp_offsets.end()) {
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
  switch(test->GetOperand()) {
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
    rewrite_instrs.push_back(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)shift));
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
      lbl_jmp_offsets.insert(instr->GetOperand());
      break;

    default:
      break;
    }
  }
  lbl_jmp_offsets.insert(unconditional_label + 1);

  for(size_t i = 0; i < input_instrs.size(); ++i) {
    IntermediateInstruction* instr = input_instrs[i];

    if(instr->GetType() == MTHD_CALL) {
      IntermediateMethod* mthd_called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
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
            if(mthd_called_instr->GetOperand2() == LOCL) {
              outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, mthd_called_instr->GetType(),
                mthd_called_instr->GetOperand() + local_instr_offset, LOCL));
            }
            else if(mthd_called_instr->GetOperand2() == INST) {
              outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, mthd_called_instr->GetType(),
                mthd_called_instr->GetOperand(), INST));
            }
            else {
              outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, mthd_called_instr->GetType(),
                mthd_called_instr->GetOperand(), CLS));
            }
            break;

          case LOAD_INST_MEM:
            outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, local_instr_offset - 1, LOCL));
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


IntermediateBlock* ItermediateOptimizer::JumpToLocation(IntermediateBlock* inputs)
{
  std::vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
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
      std::unordered_map<int, int>::iterator result = lbl_offsets.find(instr->GetOperand());
#ifdef _DEBUG
      assert(result != lbl_offsets.end());
#endif
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, result->second, instr->GetOperand2()));
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
  int int_value;

  bool set_float = false;
  double float_value;

  std::unordered_map<int, PropValue> value_prop_map;

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
        if(!(next_instr->GetType() == STOR_INT_VAR && next_next_instr->GetType() == LOAD_INT_VAR &&
           next_instr->GetOperand() == next_next_instr->GetOperand() &&
           next_instr->GetOperand2() == next_next_instr->GetOperand2())) {
          int_value = instr->GetOperand();
          set_int = true;
        }
      }
      else {
        int_value = instr->GetOperand();
        set_int = true;
      }
      break;

    case STOR_INT_VAR:
      outputs->AddInstruction(instr);
      if(set_int && instr->GetOperand2() == LOCL) {
        value_prop_map[instr->GetOperand()].int_value = int_value;
      }
      set_int = set_float = false;
      break;

    case LOAD_INT_VAR: {
      if(instr->GetOperand2() == LOCL) {
        std::unordered_map<int, PropValue>::iterator result = value_prop_map.find(instr->GetOperand());
        if(result != value_prop_map.end()) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)result->second.int_value));
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
        if(!(next_instr->GetType() == STOR_FLOAT_VAR && next_next_instr->GetType() == LOAD_FLOAT_VAR &&
           next_instr->GetOperand() == next_next_instr->GetOperand() &&
           next_instr->GetOperand2() == next_next_instr->GetOperand2())) {
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
      if(set_float && instr->GetOperand2() == LOCL) {
        value_prop_map[instr->GetOperand()].float_value = float_value;
      }
      set_int = set_float = false;
      break;

    case LOAD_FLOAT_VAR: {
      if(instr->GetOperand2() == LOCL) {
        std::unordered_map<int, PropValue>::iterator result = value_prop_map.find(instr->GetOperand());
        if(result != value_prop_map.end()) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, result->second.float_value));
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
  } else if(working_stack.size() > 1) {
    IntermediateInstruction* left = working_stack.front();
    working_stack.pop_front();

    IntermediateInstruction* right = working_stack.front();
    working_stack.pop_front();

    switch(instr->GetType()) {
    case ADD_INT: {
      INT_VALUE value = left->GetOperand() + right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;

    case SUB_INT: {
      INT_VALUE value = left->GetOperand() - right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;

    case MUL_INT: {
      INT_VALUE value = left->GetOperand() * right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;

    case DIV_INT: {
      INT_VALUE value = left->GetOperand() / right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;

    case MOD_INT: {
      INT_VALUE value = left->GetOperand() % right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;
      
    case BIT_AND_INT: {
      INT_VALUE value = left->GetOperand() & right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;

    case BIT_OR_INT: {
      INT_VALUE value = left->GetOperand() | right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
    }
      break;

    case BIT_XOR_INT: {
      INT_VALUE value = left->GetOperand() ^ right->GetOperand();
      working_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, (long)value));
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
