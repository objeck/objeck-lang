/**************************************************************************
 * Runtime debugger
 *
 * Copyright (c) 2010 Randy Hollines
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

#include "debugger.h"
#include "../loader.h"

/********************************
 * Interactive command line
 * debugger
 ********************************/
void Runtime::Debugger::ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack,
					   long call_stack_pos, StackFrame* frame)
{
  if(frame->GetMethod()->GetClass()) {
    const int line_num = instr->GetLineNumber();  
    const string &file_name = frame->GetMethod()->GetClass()->GetFileName();

#ifdef _DEBUG
    cout << "--- file=" << file_name << ", line=" << line_num << endl;
#endif

    if(line_num > -1 && line_num != cur_line_num && FindBreak(line_num, file_name)) {
      // set current line
      cur_line_num = line_num;
      cur_file_name = file_name;
      cur_frame = frame;
      cur_call_stack = call_stack;
      cur_call_stack_pos = call_stack_pos;
      
      // prompt for input
      cout << "break point: " << file_name << ":" << line_num << endl;
      Command* command;
      do {
	cout << "> ";
	string line;
	getline(cin, line);
	command = ProcessCommand(line);
	cout << endl;
      }
      while(!command || (command->GetCommandType() != CONT_COMMAND && 
			 command->GetCommandType() != NEXT_COMMAND));
    }
  }
}

void Runtime::Debugger::ProcessLoad(Load* load) {
  if(FileExists(load->GetFileName(), true)) {
    program_file = load->GetFileName();
    ClearProgram();
    cout << "loaded program file: '" << program_file << "'" << endl;
  }
  else {
    cout << "program file doesn't exist: '" << load->GetFileName() << "'" << endl;
  }
}

void Runtime::Debugger::ProcessRun() {
  // make sure file exists
  if(program_file.size() > 0) {    
    // TODO: pass args
    Loader loader(program_file.c_str()); 
    loader.Load();
  
    // execute
    op_stack = new long[STACK_SIZE];
    stack_pos = new long;
    (*stack_pos) = 0;

#ifdef _TIMING
    long start = clock();
#endif
    cur_program = loader.GetProgram();
    interpreter = new Runtime::StackInterpreter(cur_program, this);
    interpreter->Execute(op_stack, stack_pos, 0, cur_program->GetInitializationMethod(), NULL, false);
#ifdef _TIMING
    cout << "# final stack: pos=" << (*stack_pos) << " #" << endl;
    cout << "---------------------------" << endl;
    cout << "Time: " << (float)(clock() - start) / CLOCKS_PER_SEC
	 << " second(s)." << endl;
#endif
  
#ifdef _DEBUG
    cout << "# final stack: pos=" << (*stack_pos) << " #" << endl;
#endif  
    
    // clear old program
    ClearProgram();
  }
  else {
    cout << "Program file not specified" << endl;
  }
}

void Runtime::Debugger::ProcessBreak(BreakDelete* break_command) {
  int line_num = break_command->GetLineNumber();
  string file_name = break_command->GetFileName();
  
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }

  // TODO fix
  const string &path = "../../compiler/test_src/" + file_name;  
  if(FileExists(path)) {  
    if(AddBreak(line_num, file_name)) {
      cout << "added break point: " << file_name << ":" << line_num << endl;
    }
    else {
      cout << "break point already exists!" << endl;
    }
  }
  else {
    cout << "File doesn't exit: '" << path << "'" << endl;
  }
}

void Runtime::Debugger::ProcessDelete(BreakDelete* delete_command) {
  int line_num = delete_command->GetLineNumber();
  string file_name = delete_command->GetFileName();
  
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }

  // TODO fix
  const string &path = "../../compiler/test_src/" + file_name;  
  if(FileExists(path)) {  
    if(DeleteBreak(line_num, file_name)) {
      cout << "added break point: " << file_name << ":" << line_num << endl;
    }
    else {
      cout << "break point already exists!" << endl;
    }
  }
  else {
    cout << "File doesn't exit: '" << path << "'" << endl;
  }
}

void Runtime::Debugger::ProcessPrint(Print* print) {

  Expression* expression = print->GetExpression();
  EvaluateExpression(expression);

  switch(expression->GetExpressionType()) {
  case REF_EXPR: 
    if(interpreter) {
      Reference* reference = static_cast<Reference*>(expression);
      while(reference->GetReference()) {
	reference = reference->GetReference();
      }

      const StackDclr& dclr_value =  static_cast<Reference*>(reference)->GetDeclaration();
      switch(dclr_value.type) {
      case INT_PARM:
	cout << "type=Int, value=" << reference->GetIntValue() << endl;
	break;
	
      case FLOAT_PARM:
	cout << "type=Float, value=" << reference->GetFloatValue() << endl;
	break;
	  
      case BYTE_ARY_PARM:
	cout << "type=Byte:Array, value=" << (char)reference->GetIntValue() 
	     << "(" << (void*)reference->GetIntValue() << ")" << endl;
	break;

      case INT_ARY_PARM:
	cout << "type=Int:Array, value=" << reference->GetIntValue() 
	     << "(" << (void*)reference->GetIntValue() << ")" << endl;
	break;

      case FLOAT_ARY_PARM:
	cout << "type=Float:Array, value=" << reference->GetFloatValue() 
	     << "(" << (void*)reference->GetIntValue() << ")" << endl;
	break;

      case OBJ_PARM: {
	cout << "type=Object:Array, value=" << (void*)reference->GetIntValue() << endl;
      }
	break;

      case OBJ_ARY_PARM: {
	cout << "type=Object:Array, value=" << (void*)reference->GetIntValue() << endl;
      }
	break;
      }      
    }
    else {
      cout << "No program running." << endl;
    }
    break;
      
  case NIL_LIT_EXPR:
    cout << "type=Nil, value=Nil" << endl;
    break;
      
  case CHAR_LIT_EXPR:
    cout << "type=Char, value=" << (char)expression->GetIntValue() << endl;
    break;
      
  case INT_LIT_EXPR:
    cout << "type=Int, value=" << expression->GetIntValue() << endl;
    break;
      
  case FLOAT_LIT_EXPR:
    cout << "type=Float, value=" << expression->GetFloatValue() << endl;
    break;
      
  case BOOLEAN_LIT_EXPR:
    cout << "type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << endl;
    break;
      
  case AND_EXPR:
  case OR_EXPR:
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EQL_EXPR:
  case LES_EQL_EXPR:
  case GTR_EXPR:
    cout << "type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << endl;
    break;
      
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
    if(expression->GetFloatEval()) {
      cout << "type=Float, value=" << expression->GetFloatValue() << endl;
    }
    else {
      cout << "type=Int, value=" << expression->GetIntValue() << endl;
    }
    break;
      
  case CHAR_STR_EXPR:
    break;    
  }
}

void Runtime::Debugger::EvaluateExpression(Expression* expression) {
  switch(expression->GetExpressionType()) {
  case REF_EXPR:
    if(interpreter) {
      EvaluateReference(static_cast<Reference*>(expression));
    }
    else {
      cout << "No program running." << endl;
    }
    break;
    
  case NIL_LIT_EXPR:
    expression->SetIntValue(0);
    break;
      
  case CHAR_LIT_EXPR:
    expression->SetIntValue(static_cast<CharacterLiteral*>(expression)->GetValue());
    break;
      
  case INT_LIT_EXPR:
    expression->SetIntValue(static_cast<IntegerLiteral*>(expression)->GetValue());
    break;
      
  case FLOAT_LIT_EXPR:
    expression->SetFloatValue(static_cast<FloatLiteral*>(expression)->GetValue());
    break;
      
  case BOOLEAN_LIT_EXPR:
    expression->SetFloatValue(static_cast<FloatLiteral*>(expression)->GetValue());
    break;
      
  case AND_EXPR:
  case OR_EXPR:
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EQL_EXPR:
  case LES_EQL_EXPR:
  case GTR_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
    EvaluateCalculation(static_cast<CalculatedExpression*>(expression));
    break;
      
  case CHAR_STR_EXPR:
    break;      
  }
}

void Runtime::Debugger::EvaluateCalculation(CalculatedExpression* expression) {
  EvaluateExpression(expression->GetLeft());
  EvaluateExpression(expression->GetRight());

  Expression* left = expression->GetLeft();
  Expression* right = expression->GetRight();
  
  switch(expression->GetExpressionType()) {
  case AND_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() && right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() && right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() && right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() && right->GetIntValue());
    }
    break;

  case OR_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() || right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() || right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() || right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() || right->GetIntValue());
    }
    break;

  case EQL_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() == right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() == right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() == right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() == right->GetIntValue());
    }
    break;

  case NEQL_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() != right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() != right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() != right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() != right->GetIntValue());
    }
    break;

  case LES_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() < right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() < right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() < right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() < right->GetIntValue());
    }
    break;

  case GTR_EQL_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() >= right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() >= right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() >= right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() >= right->GetIntValue());
    }
    break;

  case LES_EQL_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() <= right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() <= right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() <= right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() <= right->GetIntValue());
    }
    break;

  case GTR_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() > right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() > right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() > right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() > right->GetIntValue());
    }
    break;

  case ADD_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() + right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() + right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() + right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() + right->GetIntValue());
    }
    break;

  case SUB_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() - right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() - right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() - right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() - right->GetIntValue());
    }
    break;
    
  case MUL_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() * right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() * right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() * right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() * right->GetIntValue());
    }
    break;

  case DIV_EXPR:
    if(left->GetFloatEval() && right->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() / right->GetFloatValue());
    }
    else if(left->GetFloatEval()) {
      expression->SetFloatValue(left->GetFloatValue() / right->GetIntValue());
    }
    else if(right->GetFloatEval()) {
      expression->SetFloatValue(left->GetIntValue() / right->GetFloatValue());
    }
    else {
      expression->SetIntValue(left->GetIntValue() / right->GetIntValue());
    }
    break;
    
  case MOD_EXPR:
    if(!left->GetFloatEval() && !right->GetFloatEval()) {
      expression->SetIntValue(left->GetIntValue() % right->GetIntValue());
    }
    break;
  }
}

void Runtime::Debugger::EvaluateReference(Reference* reference) {
  long* mem = cur_frame->GetMemory();
  StackMethod* method = cur_frame->GetMethod();
  
  // TODO: complex reference  
  if(reference->GetReference()) {    
    if(mem) {
      
    }
    else {
      "Unable to deference Nil frame";
    }
  }
  // simple reference
  else {
    if(mem) {
      // TODO: check for instance reference
      StackDclr dclr_value;
      const string& name = reference->GetVariableName();
      int index = method->GetDeclaration(name, dclr_value);
      reference->SetDeclaration(dclr_value);
      // check index
      if(index > -1) {
	switch(dclr_value.type) {	  
	case INT_PARM:
	  reference->SetIntValue(mem[index + 1]);
	  break;

	case FLOAT_PARM: {
	  FLOAT_VALUE value;
	  memcpy(&value, &mem[index + 1], sizeof(FLOAT_VALUE));
	  reference->SetFloatValue(value);
	}
	  break;

	case OBJ_PARM:
	  EvaluateObjectReference(reference, mem, index);
	  break;
	  
	case BYTE_ARY_PARM:
	case INT_ARY_PARM:
	case OBJ_ARY_PARM:	  
	  EvaluateIntFloatReference(reference, mem, index, false);
	  break;
	  
	case FLOAT_ARY_PARM:
	  EvaluateIntFloatReference(reference, mem, index, true);
	  break;	
	}
      }
      else {
	cout << "Unknown variable: name='" << name << "'";
      }
    }
    else {
      "Unable to deference Nil frame";
    }
  }
}

void Runtime::Debugger::EvaluateObjectReference(Reference* reference, long* mem, int index) {
}

void Runtime::Debugger::EvaluateIntFloatReference(Reference* reference, long* mem, 
						  int index, bool is_float) {
  ExpressionList* indices = reference->GetIndices();
  // de-reference array value
  if(indices) {
    long* array = (long*)mem[index + 1];
    const int max = array[0];
    const int dim = array[1];
    // calculate indices values
    vector<Expression*> expressions = indices->GetExpressions();	    
    vector<int> values;
    for(int i = 0; i < expressions.size(); i++) {
      EvaluateExpression(expressions[i]);
      // update values
      if(expressions[i]->GetFloatEval()) {
	values.push_back(expressions[i]->GetFloatValue());
      }
      else {
	values.push_back(expressions[i]->GetIntValue());
      }
    }
    // match the dimensions
    if(expressions.size() == dim) {
      // calculate indices
      array += 2;
      int j = dim - 1;
      long array_index = values[j--];
      for(long i = 1; i < dim; i++) {
	array_index *= array[i];
	array_index += values[j--];
      }	      
      array += dim;
      
      // check float array bounds
      if(is_float) {
	array_index *= 2;
	if(array_index > -1 && array_index < max * 2) {
	  FLOAT_VALUE value;
	  memcpy(&value, &array[array_index], sizeof(FLOAT_VALUE));
	  reference->SetFloatValue(value);
	}
	else {
	  cout << "Array index out of bounds." << endl;
	}
      }
      // check int array bounds
      else {
	if(array_index > -1 && array_index < max) {
	  reference->SetIntValue(array[array_index]);
	}
	else {
	  cout << "Array index out of bounds." << endl;
	}
      }
    }
    else {
      cout << "Array dimension mis-match." << endl;
    }
  }
  // set array address
  else {
    reference->SetIntValue(mem[index + 1]);
  }
}

Command* Runtime::Debugger::ProcessCommand(const string &line) {
#ifdef _DEBUG
  cout << "input: |" << line << "|" << endl;
#endif
  
  // parser input
  Parser parser;  
  Command* command = parser.Parse("?" + line);
  if(command) {
    switch(command->GetCommandType()) {
    case LOAD_COMMAND:
      ProcessLoad(static_cast<Load*>(command));
      break;
      
    case QUIT_COMMAND:
      ClearBreaks();
      ClearProgram();
      cout << "Goodbye!" << endl;
      exit(0);
      break;

    case BREAK_COMMAND:
      ProcessBreak(static_cast<BreakDelete*>(command));
      break;

    case PRINT_COMMAND:
      ProcessPrint(static_cast<Print*>(command));
      break;

    case RUN_COMMAND:
      ProcessRun();
      break;
      
    case CLEAR_COMMAND: {
      cout << "Are sure you want to clear all break points? [y/n] ";
      string line;
      getline(cin, line);      
      if(line == "y" || line == "yes") {
	cout << "Break points cleared." << endl;
	ClearBreaks();
      }
      cout << endl;
    }
      break;

    case DELETE_COMMAND:
      ProcessDelete(static_cast<BreakDelete*>(command));
      break;
      
    case INFO_COMMAND:
      break;
      
    case FRAME_COMMAND:
      break;
    }  

    return command;
  }
  else {
    cout << "Unable to process command" << endl;
  }
  
  return NULL;
}

void Runtime::Debugger::ClearBreaks() {
  while(!breaks.empty()) {
    UserBreak* tmp = breaks.front();
    breaks.erase(breaks.begin());
    // delete
    delete tmp;
    tmp = NULL;
  }
}

void Runtime::Debugger::ClearProgram() {
  if(interpreter) {
    delete interpreter;
    interpreter = NULL;
  }
  
  if(op_stack) {
    delete[] op_stack;
    op_stack = NULL;
  }
  
  if(stack_pos) {
    delete stack_pos;
    stack_pos = NULL;
  }
  
  cur_line_num = -2;
  cur_frame = NULL;
  cur_program = NULL;
  cur_call_stack = NULL;
  cur_call_stack_pos = NULL;
}

Runtime::Debugger::Debugger(const string &fn) {
  quit = false;
  program_file = fn;
  interpreter = NULL;
  op_stack = NULL;
  stack_pos = NULL;
  cur_line_num = -2;
  cur_frame = NULL;
  cur_program = NULL;
  cur_call_stack = NULL;
  cur_call_stack_pos = NULL;
}

Runtime::Debugger::~Debugger() {
  ClearProgram();
  ClearBreaks();
}

/********************************
 * Debugger main
 ********************************/
int main(int argc, char** argv) 
{
  string file_name;
  if(argc == 2) {
    file_name = argv[1];
  }
  Runtime::Debugger debugger(file_name);
  debugger.Debug();
}
