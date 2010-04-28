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

    if(line_num > -1 && line_num != cur_line_num && file_name != cur_file_name && 
       FindBreak(line_num, file_name)) {
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
    interpreter = new Runtime::StackInterpreter(loader.GetProgram(), this);
    interpreter->Execute(op_stack, stack_pos, 0, loader.GetProgram()->GetInitializationMethod(), NULL, false);
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
  if(interpreter) {
    cout << "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" << endl;
    
    Expression* expression = print->GetExpression();
    switch(expression->GetExpressionType()) {
    case REF_EXPR:
      EvaluateReference(static_cast<Reference*>(expression));
      break;
      
    case NIL_LIT_EXPR:
      break;
      
    case CHAR_LIT_EXPR:
      break;
      
    case INT_LIT_EXPR:
      break;
      
    case FLOAT_LIT_EXPR:
      break;
      
    case BOOLEAN_LIT_EXPR:
      break;
      
    case AND_EXPR:
      break;
      
    case OR_EXPR:
      break;
      
    case EQL_EXPR:
      break;
      
    case NEQL_EXPR:
      break;
      
    case LES_EXPR:
      break;
      
    case GTR_EQL_EXPR:
      break;
      
    case LES_EQL_EXPR:
      break;
      
    case GTR_EXPR:
      break;
      
    case ADD_EXPR:
      break;
      
    case SUB_EXPR:
      break;
      
    case MUL_EXPR:
      break;
      
    case DIV_EXPR:
      break;
      
    case MOD_EXPR:
      break;
      
    case CHAR_STR_EXPR:
      break;      
    }
  }
  else {
    cout << "No program running." << endl;
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
      "Unable to deference Nil frame value";
    }
  }
  // simple reference
  else {
    if(mem) {
      const string& name = reference->GetVariableName();
      StackDclr dclr_value;
      int index = method->GetDeclaration(name, dclr_value);
      if(index > -1) {
	switch(dclr_value.type) {	  
	case INT_PARM:
	  cout << "name=" << dclr_value.name << ", type=Int, value=" << mem[index + 1] << endl;
	  break;

	case FLOAT_PARM: {
	  FLOAT_VALUE value;
	  memcpy(&value, &mem[index + 1], sizeof(FLOAT_VALUE));
	  cout << "name=" << dclr_value.name << ", type=Float, value=" << value << endl;
	}
	  break;
	  
	case BYTE_ARY_PARM:
	  cout << "name=" << dclr_value.name << ", type=Byte:Array, value=" << (void*)mem[index + 1] << endl;
	  break;

	case INT_ARY_PARM:
	  cout << "name=" << dclr_value.name << ", type=Int:Array, value=" << (void*)mem[index + 1] << endl;
	  break;

	case FLOAT_ARY_PARM:
	  cout << "name=" << dclr_value.name << ", type=Float:Array, value=" << (void*)mem[index + 1] << endl;
	  break;

	case OBJ_PARM:
	  cout << "name=" << dclr_value.name << ", type=Object, value=" << (void*)mem[index + 1] << endl;
	  break;

	case OBJ_ARY_PARM:
	  cout << "name=" << dclr_value.name << ", type=Object:Array, value=" << (void*)mem[index + 1] << endl;
	  break;
	}
      }
      else {
	"Unknown variable";
      }
    }
    else {
      "Unable to deference Nil frame value";
    }
  }
}

Command* Runtime::Debugger::ProcessCommand(const string &line) {
#ifdef _DEBUG
  cout << "input line: |" << line << "|" << endl;
#endif
  
  // parser input
  Parser parser;  
  Command* command = parser.Parse(line);
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
      cout << "  Are sure you want to clear all break points? [y/n] ";
      string line;
      getline(cin, line);      
      if(line == "y" || line == "yes") {
	cout << "  Break points cleared." << endl;
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
