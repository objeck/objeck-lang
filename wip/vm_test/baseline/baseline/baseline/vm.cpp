#include "vm.h"

#define POP_INT() (op_stack[--(*stack_pos)])
#define PUSH_INT(v) (op_stack[(*stack_pos)++] = v)

void ExecuteSwitch(Instruction** instrs, long ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table) {
  long mem[8]; // space for local variables
  memset(mem, 0, sizeof(long) * 8); 
  long left, right;

  bool halt = false;
  do {
#ifdef _DEBUG		
    assert((*stack_pos) > -1);
#endif

    Instruction* instr = instrs[ip++];
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      PUSH_INT(instr->GetOperand());
      break;

    case LOAD_INT_VAR:
      PUSH_INT(mem[instr->GetOperand() + 1]);
      break;

    case STOR_INT_VAR:
      mem[instr->GetOperand() + 1] = POP_INT();
      break;

    case ADD_INT:
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right + left);
      break;

    case LES_INT:
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right < left);
      break;

    case MUL_INT:
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right * left);
      break;

    case JMP:
      if(!instr->GetOperand3()) {
        if(instr->GetOperand2() < 0) {
          // ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
          std::map<long, size_t>::iterator found = jump_table.find(instr->GetOperand());
#ifdef _DEBUG
          assert(found != jump_table.end());
#endif
          ip = found->second + 1;
          instr->SetOperand3(ip);
        } 
        else if(POP_INT() == instr->GetOperand2()) {
          std::map<long, size_t>::iterator found = jump_table.find(instr->GetOperand());
#ifdef _DEBUG
          assert(found != jump_table.end());
#endif
          ip = found->second + 1;
          instr->SetOperand3(ip);
        }
      }
      else {
        if(instr->GetOperand2() < 0) {
          ip = instr->GetOperand3();
        } 
        else if(POP_INT() == instr->GetOperand2()) {
          ip = instr->GetOperand3();
        }
      }
      break;

    case RTRN:
      halt = true;
      break;

    default:
      break;
    }
  }
  while(!halt);
}

void Execute2(Instruction** instrs, long ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table) {
  long mem[8]; // space for local variables
  memset(mem, 0, sizeof(long) * 8); 
  long left, right;

  bool halt = false;
  do {
#ifdef _DEBUG		
    assert((*stack_pos) > -1);
#endif

    Instruction* instr = instrs[ip++];
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      PUSH_INT(instr->GetOperand());
      break;

    case LOAD_INT_VAR:
      PUSH_INT(mem[instr->GetOperand() + 1]);
      break;

    case STOR_INT_VAR:
      mem[instr->GetOperand() + 1] = POP_INT();
      break;

    case ADD_INT:
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right + left);
      break;

    case LES_INT:
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right < left);
      break;

    case MUL_INT:
      right = POP_INT();
      left = POP_INT();
      PUSH_INT(right * left);
      break;

    case JMP:
      if(!instr->GetOperand3()) {
        if(instr->GetOperand2() < 0) {
          // ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
          std::map<long, size_t>::iterator found = jump_table.find(instr->GetOperand());
#ifdef _DEBUG
          assert(found != jump_table.end());
#endif
          ip = found->second + 1;
          instr->SetOperand3(ip);
        } 
        else if(POP_INT() == instr->GetOperand2()) {
          std::map<long, size_t>::iterator found = jump_table.find(instr->GetOperand());
#ifdef _DEBUG
          assert(found != jump_table.end());
#endif
          ip = found->second + 1;
          instr->SetOperand3(ip);
        }
      }
      else {
        if(instr->GetOperand2() < 0) {
          ip = instr->GetOperand3();
        } 
        else if(POP_INT() == instr->GetOperand2()) {
          ip = instr->GetOperand3();
        }
      }
      break;

    case RTRN:
      halt = true;
      break;

    default:
      break;
    }
  }
  while(!halt);
}

///////////////////////////////////////////////

void LoadIntLit(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  Instruction* instr = instrs[ip];
  PUSH_INT(instr->GetOperand());
  ip++;

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void LoadIntVar(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  Instruction* instr = instrs[ip];
  PUSH_INT(mem[instr->GetOperand() + 1]);
  ip++;

 #ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void StorIntVar(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  Instruction* instr = instrs[ip];
  mem[instr->GetOperand() + 1] = POP_INT();
  ip++;

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void AddInt(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  long right = POP_INT();
  long left = POP_INT();
  PUSH_INT(right + left);
  ip++;

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void LesInt(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  long right = POP_INT();
  long left = POP_INT();
  PUSH_INT(right < left);
  ip++;

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void	MulInt(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  long right = POP_INT();
  long left = POP_INT();
  PUSH_INT(right * left);
  ip++;

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void Label(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  ip++;

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void Jump(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
  Instruction* instr = instrs[ip];
  if(!instr->GetOperand3()) {
    if(instr->GetOperand2() < 0) {
      // ip = frame->GetMethod()->GetLabelIndex(instr->GetOperand()) + 1;
      std::map<long, size_t>::iterator found = jump_table.find(instr->GetOperand());
#ifdef _DEBUG
      assert(found != jump_table.end());
#endif
      ip = found->second + 1;
      instr->SetOperand3(ip);
    } 
    else if(POP_INT() == instr->GetOperand2()) {
      std::map<long, size_t>::iterator found = jump_table.find(instr->GetOperand());
#ifdef _DEBUG
      assert(found != jump_table.end());
#endif
      ip = found->second + 1;
      instr->SetOperand3(ip);
    }
    else {
      ip++;
    }
  }
  else {
    if(instr->GetOperand2() < 0) {
      ip = instr->GetOperand3();
    } 
    else if(POP_INT() == instr->GetOperand2()) {
      ip = instr->GetOperand3();
    }
    else {
      ip++;
    }
  }

#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void Return(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers) {
#ifdef _DEBUG
  assert(*stack_pos > -1);
#endif
}

void ExecuteThreadedFunctions(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table) {
  void* handlers[9];
  handlers[0] = LoadIntLit;
  handlers[1] = LoadIntVar;
  handlers[2] = StorIntVar;
  handlers[3] = AddInt;
  handlers[4] = LesInt;
  handlers[5] = MulInt;
  handlers[6] = Label;
  handlers[7] = Jump;
  handlers[8] = Return;

  long* mem = new long[8]; // space for local variables
  memset(mem, 0, sizeof(long) * 8); 

  size_t i = 0;
  do {
    handler_def handler = (handler_def)handlers[instrs[ip]->GetType()];
    (*handler)(instrs, ip, op_stack, stack_pos, jump_table, mem, handlers);
    i++;
  }
  while(instrs[ip]->GetType() != RTRN);
}

int main() {
  // initialize jump table
  Instruction** instrs = LoadInstructions();
  std::map<long, size_t> jump_table;
  long index = 0;
  for(long i = 0; i < MAX_INSTRS; i++) {
    Instruction* instr = instrs[i];
    if(instr->GetType() == LBL) {
      jump_table.insert(std::pair<long, size_t>(instr->GetOperand(), i));
    }
  }

  // setup execution stack
  long ip =  0;
  long* op_stack = new long[32];
  long* stack_pos = new long;
  *stack_pos = 0;
  size_t* labels = new size_t[RTRN + 1];

  clock_t start = clock();

  ExecuteThreadedFunctions(instrs, ip, op_stack, stack_pos, jump_table);
  // ExecuteSwitch(instrs, ip, op_stack, stack_pos, jump_table);
  // ExecuteAsm(NULL, ip, op_stack, stack_pos, jump_table, labels);
  // ExecuteAsm(instrs, ip, op_stack, stack_pos, jump_table, labels);

  clock_t end = clock();
  std::cout << "---------------------------" << std::endl;
  std::cout << "CPU time=" << (double)(end - start) / CLOCKS_PER_SEC << " second(s)." << std::endl;

  // print final result
  std::cout << "result=" << op_stack[--(*stack_pos)] << ", pos=" << *stack_pos << std::endl;
}