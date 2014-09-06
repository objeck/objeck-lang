#include <iostream>
#include <map>
#include "time.h"
#include "assert.h"

#define MAX_INSTRS 34
// #define MAX_INSTRS 4
#define LOOP_MAX 10000
// #define LOOP_MAX 10

enum InstructionType 
{
  LOAD_INT_LIT,
  LOAD_INT_VAR,
  STOR_INT_VAR,
  ADD_INT,
  LES_INT,
  MUL_INT,
  LBL,
  JMP,
  RTRN
};

class Instruction {
  InstructionType type;
  long operand;
  long operand2;
  long operand3;

public:
  Instruction(InstructionType t, long o, long o2) {
    type = t;
    operand = o;
    operand2 = o2;
    operand3 = 0;
  }

  Instruction(InstructionType t, long o) {
    type = t;
    operand = o;
    operand2 = operand3 = 0;
  }

  Instruction(InstructionType t) {
    type = t;
    operand = operand2 = 0;
    operand3 = 0;
  }

  ~Instruction() {}

  InstructionType GetType() {
    return type;
  }

  long GetOperand() {
    return operand;
  }

  long GetOperand2() {
    return operand2;
  }

  long GetOperand3() {
    return operand3;
  }

  void SetOperand3(long o3) {
    operand3 = o3;
  }
};

// switch/case
void ExecuteSwitch(Instruction** instrs, long ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table);

// thread asm
extern "C" {
  void ExecuteAsm(Instruction** instrs, long ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, size_t* labels);
}

// thread functions
void ExecuteThreadedFunctions(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table);
typedef void (*handler_def)(Instruction**, long &, long*, long*, std::map<long, size_t> &, long*, void**);
void LoadIntLit(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void LoadIntVar(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void StorIntVar(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void AddInt(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void LesInt(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void	MulInt(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void Label(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void Jump(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);
void Return(Instruction** instrs, long &ip, long* op_stack, long* stack_pos, std::map<long, size_t> &jump_table, long* mem, void** handlers);

Instruction** LoadInstructions() {
  Instruction** instrs = new Instruction*[MAX_INSTRS];

/*
  instrs[0] = new Instruction(LOAD_INT_LIT, 7);
  instrs[1] = new Instruction(STOR_INT_VAR, 1, 1);
  instrs[2] = new Instruction(LOAD_INT_LIT, 14);
  instrs[3] = new Instruction(LOAD_INT_VAR, 1, 1);
  instrs[4] = new Instruction(ADD_INT);
  instrs[5] = new Instruction(LOAD_INT_LIT, 3);
  instrs[6] = new Instruction(MUL_INT);
  instrs[7] = new Instruction(RTRN);
*/

  instrs[0] = new Instruction(LOAD_INT_LIT, 0);
  instrs[1] = new Instruction(STOR_INT_VAR, 1, 1);
  instrs[2] = new Instruction(LBL, 1073741824);
  instrs[3] = new Instruction(LOAD_INT_LIT, LOOP_MAX);
  instrs[4] = new Instruction(LOAD_INT_VAR, 1, 1);
  instrs[5] = new Instruction(LES_INT);
  instrs[6] = new Instruction(JMP, 0, 0);
  instrs[7] = new Instruction(LOAD_INT_LIT, 0);
  instrs[8] = new Instruction(STOR_INT_VAR, 2, 1);
  instrs[9] = new Instruction(LBL, 1073741825);
  instrs[10] = new Instruction(LOAD_INT_LIT, LOOP_MAX);
  instrs[11] = new Instruction(LOAD_INT_VAR, 2, 1);
  instrs[12] = new Instruction(LES_INT);
  instrs[13] = new Instruction(JMP, 1, 0);
  instrs[14] = new Instruction(LOAD_INT_VAR, 2, 1);
  instrs[15] = new Instruction(LOAD_INT_VAR, 1, 1);
  instrs[16] = new Instruction(MUL_INT);
  instrs[17] = new Instruction(LOAD_INT_VAR, 0, 1);
  instrs[18] = new Instruction(ADD_INT);
  instrs[19] = new Instruction(STOR_INT_VAR, 0, 1);
  instrs[20] = new Instruction(LOAD_INT_LIT, 1);
  instrs[21] = new Instruction(LOAD_INT_VAR, 2, 1);
  instrs[22] = new Instruction(ADD_INT);
  instrs[23] = new Instruction(STOR_INT_VAR, 2, 1);
  instrs[24] = new Instruction(JMP, 1073741825, -1);
  instrs[25] = new Instruction(LBL, 1);
  instrs[26] = new Instruction(LOAD_INT_LIT, 1);
  instrs[27] = new Instruction(LOAD_INT_VAR, 1, 1);
  instrs[28] = new Instruction(ADD_INT);
  instrs[29] = new Instruction(STOR_INT_VAR, 1, 1);
  instrs[30] = new Instruction(JMP, 1073741824, -1);
  instrs[31] = new Instruction(LBL, 0);
  instrs[32] = new Instruction(LOAD_INT_VAR, 0, 1);
  instrs[33] = new Instruction(RTRN);

  return instrs;
}

