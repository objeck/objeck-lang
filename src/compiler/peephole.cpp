/***************************************************************************
 * Platform independent language optimizer.
 *
 * Copyright (c) 2008, 2009, 2010 Randy Hollines
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
 * - Neither the name of the StackVM Team nor the names of its
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

#include "peephole.h"

using namespace backend;

void ItermediateOptimizer::Optimize()
{
  if(optimization_level > 0) {
#ifdef _DEBUG
    cout << "\n--------- Optimizing Code ---------" << endl;
#endif

    // classes...
    vector<IntermediateClass*> klasses = program->GetClasses();
    for(unsigned int i = 0; i < klasses.size(); i++) {
      // methods...
      vector<IntermediateMethod*> methods = klasses[i]->GetMethods();
      for(unsigned int j = 0; j < methods.size(); j++) {
        current_method = methods[j];
	// constant folding
#ifdef _DEBUG
	cout << "Optimizing method: name='" << current_method->GetName() << "'" << endl;
#endif
	current_method->SetBlocks(OptimizeMethod(current_method->GetBlocks()));
      }
    }
  }
}

vector<IntermediateBlock*> ItermediateOptimizer::OptimizeMethod(vector<IntermediateBlock*> inputs)
{
  vector<IntermediateBlock*> folded_float_blocks;
  if(optimization_level > 0) {
    // fold integers
#ifdef _DEBUG
    cout << "  Folding integers..." << endl;
#endif
    vector<IntermediateBlock*> folded_int_blocks;
    while(!inputs.empty()) {
      IntermediateBlock* tmp = inputs.front();
      folded_int_blocks.push_back(FoldIntConstants(tmp));
      // delete old block
      inputs.erase(inputs.begin());
      delete tmp;
      tmp = NULL;
    }

    // fold floats
#ifdef _DEBUG
    cout << "  Folding floats..." << endl;
#endif
    while(!folded_int_blocks.empty()) {
      IntermediateBlock* tmp = folded_int_blocks.front();
      folded_float_blocks.push_back(FoldFloatConstants(tmp));
      // delete old block
      folded_int_blocks.erase(folded_int_blocks.begin());
      delete tmp;
      tmp = NULL;
    }
  } else {
    return inputs;
  }

  vector<IntermediateBlock*> strength_reduced_blocks;
  if(optimization_level > 1) {
    // reduce strength
#ifdef _DEBUG
    cout << "  Strength reduction..." << endl;
#endif
    while(!folded_float_blocks.empty()) {
      IntermediateBlock* tmp = folded_float_blocks.front();
      strength_reduced_blocks.push_back(StrengthReduction(tmp));
      // delete old block
      folded_float_blocks.erase(folded_float_blocks.begin());
      delete tmp;
      tmp = NULL;
    }
  } else {
    return folded_float_blocks;
  }

  vector<IntermediateBlock*> instruction_replaced_blocks;
  if(optimization_level > 2) {
    // instruction replacement
#ifdef _DEBUG
    cout << "  Instruction replacement..." << endl;
#endif
    while(!strength_reduced_blocks.empty()) {
      IntermediateBlock* tmp = strength_reduced_blocks.front();
      instruction_replaced_blocks.push_back(InstructionReplacement(tmp));
      // delete old block
      strength_reduced_blocks.erase(strength_reduced_blocks.begin());
      delete tmp;
      tmp = NULL;
    }
  } 
  else {
    return strength_reduced_blocks;
  }

  return instruction_replaced_blocks;

  /*  
  vector<IntermediateBlock*> method_lnlined_blocks;
  if(optimization_level > 3) {
    // instruction replacement
#ifdef _DEBUG
    cout << "  Method inlining..." << endl;
#endif
    while(!instruction_replaced_blocks.empty()) {
      IntermediateBlock* tmp = instruction_replaced_blocks.front();
      method_lnlined_blocks.push_back(InlineMethodCall(tmp));
      // delete old block
      instruction_replaced_blocks.erase(instruction_replaced_blocks.begin());
      delete tmp;
      tmp = NULL;
    }
  } 
  else {
    return instruction_replaced_blocks;
  }
  
  return method_lnlined_blocks;
  */
}

// Note: this code collapses basic block... refractor so optimizations can be re-ordered 
IntermediateBlock* ItermediateOptimizer::InlineMethodCall(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;

  vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(unsigned int i = 0; i < input_instrs.size(); i++) {
    IntermediateInstruction* instr = input_instrs[i];
    if(instr->GetType() == MTHD_CALL) {
      IntermediateMethod* called = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
      if(!called->IsVirtual() && called->GetInstructionCount() < 16 && 
	 !(current_method->GetClass()->GetId() == program->GetStartClassId() && 
	   current_method->GetId() == program->GetStartMethodId()) &&
	 // TODO: pass constructor flag
	 current_method->GetName().find(current_method->GetClass()->GetName() + ":New") != 0) {
        InlineMethodCall(called, outputs);
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

void ItermediateOptimizer::InlineMethodCall(IntermediateMethod* called, IntermediateBlock* outputs)
{
  const int locl_offset = current_method->GetSpace() / sizeof(INT_VALUE);
  current_method->SetSpace(current_method->GetSpace() + called->GetSpace() + sizeof(INT_VALUE));
  
  int inst_offset = 0;
  if(current_method->GetClass()->GetId() != called->GetClass()->GetId()) {
    inst_offset = current_method->GetClass()->GetInstanceSpace() / sizeof(INT_VALUE);
    current_method->GetClass()->SetInstanceSpace(current_method->GetClass()->GetInstanceSpace() + called->GetClass()->GetInstanceSpace());
  }
  
  int cls_offset = 0;
  if(current_method->GetClass()->GetId() != called->GetClass()->GetId()) {
    cls_offset = current_method->GetClass()->GetClassSpace() / sizeof(INT_VALUE);
    current_method->GetClass()->SetClassSpace(current_method->GetClass()->GetClassSpace() + called->GetClass()->GetClassSpace());
  }
  
  // manage LOAD_INST_MEM for callee
  outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, STOR_INT_VAR, locl_offset, LOCL));

  bool needs_jump = false;
  vector<IntermediateBlock*> blocks = called->GetBlocks();
  for(unsigned int i = 0; i < blocks.size(); i++) {
    IntermediateBlock* block = blocks[i];
    vector<IntermediateInstruction*> instrs = block->GetInstructions();
    for(unsigned int j = 0; j < instrs.size(); j++) {
      IntermediateInstruction* instr = instrs[j];
      switch(instr->GetType()) {        
      case LOAD_INT_VAR:
      case STOR_INT_VAR:
      case COPY_INT_VAR:
      case LOAD_FLOAT_VAR:
      case STOR_FLOAT_VAR:
      case COPY_FLOAT_VAR: {
        if(instr->GetOperand2() == LOCL) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, instr->GetType(), instr->GetOperand() + locl_offset + 1, instr->GetOperand2()));
        }
	else if(instr->GetOperand2() == CLS) {
	  outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, instr->GetType(), instr->GetOperand() + cls_offset, instr->GetOperand2()));
	}
	else {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, instr->GetType(), instr->GetOperand() + inst_offset, instr->GetOperand2()));
        }
      }
	break;
      
      case LOAD_INST_MEM:
        outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_VAR, locl_offset, LOCL));
        break;
	
      case RTRN:
        // note: code generates an extra empty block if there's more than 1 block
        if(!((blocks.size() == 1 || i == blocks.size() - 2) && j == instrs.size() - 1)) {
          outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, JMP, inline_end, -1));
          needs_jump = true;
        } 
	else {
          merge_blocks = true;
        }
        break;
	
      default:
        outputs->AddInstruction(instr);
        break;
      }
    }
  }

  if(needs_jump) {
    outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LBL, inline_end--));
  }
}

IntermediateBlock* ItermediateOptimizer::InstructionReplacement(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  list<IntermediateInstruction*> calc_stack;

  unsigned int i = 0;
  vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  while(i < input_instrs.size() && (input_instrs[i]->GetType() == STOR_INT_VAR ||
                                    input_instrs[i]->GetType() == STOR_FLOAT_VAR)) {
    outputs->AddInstruction(input_instrs[i++]);
  }

  for(; i < input_instrs.size(); i++) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
      ReplacementInstruction(instr, calc_stack, outputs);
      break;

    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
      calc_stack.push_front(instr);
      break;

    default:
      // order matters...
      while(!calc_stack.empty()) {
        outputs->AddInstruction(calc_stack.back());
        calc_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!calc_stack.empty()) {
    outputs->AddInstruction(calc_stack.back());
    calc_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::ReplacementInstruction(IntermediateInstruction* instr,
    list<IntermediateInstruction*> &calc_stack,
    IntermediateBlock* outputs)
{
  if(!calc_stack.empty()) {
    IntermediateInstruction* top_instr = calc_stack.front();
    if(top_instr->GetType() == STOR_INT_VAR && instr->GetType() == LOAD_INT_VAR &&
        instr->GetOperand() == top_instr->GetOperand() &&
        instr->GetOperand2() == top_instr->GetOperand2()) {
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, COPY_INT_VAR, top_instr->GetOperand(), top_instr->GetOperand2()));
      calc_stack.pop_front();
    } else if(top_instr->GetType() == STOR_FLOAT_VAR && instr->GetType() == LOAD_FLOAT_VAR &&
              instr->GetOperand() == top_instr->GetOperand() &&
              instr->GetOperand2() == top_instr->GetOperand2()) {
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, COPY_FLOAT_VAR, top_instr->GetOperand(), top_instr->GetOperand2()));
      calc_stack.pop_front();
    } else {
      // order matters...
      while(!calc_stack.empty()) {
        outputs->AddInstruction(calc_stack.back());
        calc_stack.pop_back();
      }
      outputs->AddInstruction(instr);
    }
  } else {
    outputs->AddInstruction(instr);
  }
}

IntermediateBlock* ItermediateOptimizer::StrengthReduction(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  list<IntermediateInstruction*> calc_stack;

  vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(unsigned int i = 0; i < input_instrs.size(); i++) {
    IntermediateInstruction* instr = input_instrs[i];
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      calc_stack.push_front(instr);
      break;

    case LOAD_INT_VAR:
      calc_stack.push_front(instr);
      break;

    case MUL_INT:
    case DIV_INT:
      CalculateReduction(instr, calc_stack, outputs);
      break;

    default:
      // order matters...
      while(!calc_stack.empty()) {
        outputs->AddInstruction(calc_stack.back());
        calc_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!calc_stack.empty()) {
    outputs->AddInstruction(calc_stack.back());
    calc_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::CalculateReduction(IntermediateInstruction* instr,
    list<IntermediateInstruction*> &calc_stack,
    IntermediateBlock* outputs)
{
  if(calc_stack.size() > 1) {
    IntermediateInstruction* top_instr = calc_stack.front();
    calc_stack.pop_front();
    if(top_instr->GetType() == LOAD_INT_LIT) {
      if(calc_stack.front()->GetType() == LOAD_INT_VAR) {
        ApplyReduction(top_instr, instr, top_instr, calc_stack, outputs);
      } else {
        AddBackReduction(instr, top_instr, calc_stack, outputs);
      }
    } else if(calc_stack.front()->GetType() == LOAD_INT_LIT) {
      if(top_instr->GetType() == LOAD_INT_VAR) {
        ApplyReduction(calc_stack.front(), instr, top_instr, calc_stack, outputs);
      } else {
        AddBackReduction(instr, top_instr, calc_stack, outputs);
      }
    } else {
      AddBackReduction(instr, top_instr, calc_stack, outputs);
    }
  } else {
    // order matters...
    while(!calc_stack.empty()) {
      outputs->AddInstruction(calc_stack.back());
      calc_stack.pop_back();
    }
    outputs->AddInstruction(instr);
  }
}

void ItermediateOptimizer::ApplyReduction(IntermediateInstruction* test, IntermediateInstruction* instr, IntermediateInstruction* top_instr,
    list<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs)
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

  default:
    AddBackReduction(instr, top_instr, calc_stack, outputs);
    break;
  }

  if(shift) {
    // exclude literal
    if(calc_stack.front()->GetType() != LOAD_INT_LIT) {
      outputs->AddInstruction(calc_stack.front());
    }
    if(top_instr->GetType() != LOAD_INT_LIT) {
      outputs->AddInstruction(top_instr);
    }
    calc_stack.pop_front();
    // shift left or right
    if(instr->GetType() == MUL_INT) {
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, SHL_INT, shift));
    } else {
      outputs->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, SHR_INT, shift));
    }
  }
  // add rest of information
  while(!calc_stack.empty()) {
    outputs->AddInstruction(calc_stack.back());
    calc_stack.pop_back();
  }
}

void ItermediateOptimizer::AddBackReduction(IntermediateInstruction* instr, IntermediateInstruction* top_instr,
    list<IntermediateInstruction*> &calc_stack, IntermediateBlock* outputs)
{
  // order matters...
  while(!calc_stack.empty()) {
    outputs->AddInstruction(calc_stack.back());
    calc_stack.pop_back();
  }
  outputs->AddInstruction(top_instr);
  outputs->AddInstruction(instr);
}

IntermediateBlock* ItermediateOptimizer::FoldIntConstants(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  list<IntermediateInstruction*> calc_stack;

  vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(unsigned int i = 0; i < input_instrs.size(); i++) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      calc_stack.push_front(instr);
      break;

    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
      CalculateIntFold(instr, calc_stack, outputs);
      break;

    default:
      // add back in reverse order
      while(!calc_stack.empty()) {
        outputs->AddInstruction(calc_stack.back());
        calc_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!calc_stack.empty()) {
    outputs->AddInstruction(calc_stack.back());
    calc_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::CalculateIntFold(IntermediateInstruction* instr,
    list<IntermediateInstruction*> &calc_stack,
    IntermediateBlock* outputs)
{
  if(calc_stack.size() == 1) {
    outputs->AddInstruction(calc_stack.front());
    calc_stack.pop_front();
    outputs->AddInstruction(instr);
  } else if(calc_stack.size() > 1) {
    IntermediateInstruction* left = calc_stack.front();
    calc_stack.pop_front();

    IntermediateInstruction* right = calc_stack.front();
    calc_stack.pop_front();

    switch(instr->GetType()) {
    case ADD_INT: {
      INT_VALUE value = left->GetOperand() + right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;

    case SUB_INT: {
      INT_VALUE value = left->GetOperand() - right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;

    case MUL_INT: {
      INT_VALUE value = left->GetOperand() * right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;

    case DIV_INT: {
      INT_VALUE value = left->GetOperand() / right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;

    case MOD_INT: {
      INT_VALUE value = left->GetOperand() % right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;
      
    case BIT_AND_INT: {
      INT_VALUE value = left->GetOperand() & right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;

    case BIT_OR_INT: {
      INT_VALUE value = left->GetOperand() | right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;

    case BIT_XOR_INT: {
      INT_VALUE value = left->GetOperand() ^ right->GetOperand();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_INT_LIT, value));
    }
      break;
    }
  } else {
    outputs->AddInstruction(instr);
  }
}

IntermediateBlock* ItermediateOptimizer::FoldFloatConstants(IntermediateBlock* inputs)
{
  IntermediateBlock* outputs = new IntermediateBlock;
  list<IntermediateInstruction*> calc_stack;

  vector<IntermediateInstruction*> input_instrs = inputs->GetInstructions();
  for(unsigned int i = 0; i < input_instrs.size(); i++) {
    IntermediateInstruction* instr = input_instrs[i];

    switch(instr->GetType()) {
    case LOAD_FLOAT_LIT:
      calc_stack.push_front(instr);
      break;

    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
      CalculateFloatFold(instr, calc_stack, outputs);
      break;

    default:
      // add back in reverse order
      while(!calc_stack.empty()) {
        outputs->AddInstruction(calc_stack.back());
        calc_stack.pop_back();
      }
      outputs->AddInstruction(instr);
      break;
    }
  }
  // order matters...
  while(!calc_stack.empty()) {
    outputs->AddInstruction(calc_stack.back());
    calc_stack.pop_back();
  }

  return outputs;
}

void ItermediateOptimizer::CalculateFloatFold(IntermediateInstruction* instr,
    list<IntermediateInstruction*> &calc_stack,
    IntermediateBlock* outputs)
{
  if(calc_stack.size() == 1) {
    outputs->AddInstruction(calc_stack.front());
    calc_stack.pop_front();
    outputs->AddInstruction(instr);
  } else if(calc_stack.size() > 1) {
    IntermediateInstruction* left = calc_stack.front();
    calc_stack.pop_front();

    IntermediateInstruction* right = calc_stack.front();
    calc_stack.pop_front();

    switch(instr->GetType()) {
    case ADD_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() + right->GetOperand4();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
    break;

    case SUB_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() - right->GetOperand4();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
    break;

    case MUL_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() * right->GetOperand4();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
    break;

    case DIV_FLOAT: {
      FLOAT_VALUE value = left->GetOperand4() / right->GetOperand4();
      calc_stack.push_front(IntermediateFactory::Instance()->MakeInstruction(cur_line_num, LOAD_FLOAT_LIT, value));
    }
    break;
    }
  } else {
    outputs->AddInstruction(instr);
  }
}
