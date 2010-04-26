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
  cout << "#####################################" << endl;
}

void Runtime::Debugger::ProcessLoad(Load* load) {
  // TODO: pass args
  Loader loader(load->GetFileName().c_str()); 
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
}

void Runtime::Debugger::ProcessBreak(Break* break_command) {

}

void Runtime::Debugger::ProcessPrint(Print* print) {
  
}

bool Runtime::Debugger::ProcessCommand(const string &line) {
#ifdef _DEBUG
  cout << "line: |" << line << "|" << endl;
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
      return true;

    case BREAK_COMMAND:
      ProcessBreak(static_cast<Break*>(command));
      break;

    case PRINT_COMMAND:
      ProcessPrint(static_cast<Print*>(command));
      break;

    case INFO_COMMAND:
      break;

    case FRAME_COMMAND:
      break;
    }    
  }
  else {
    cout << "Unable to process command" << endl;
  }

  return false;
}

Runtime::Debugger::Debugger() {
  interpreter = NULL;
  long* op_stack = NULL;
  long* stack_pos = NULL;
}

Runtime::Debugger::~Debugger() {
  if(interpreter) {
    delete interpreter;
    interpreter = NULL;
    
    delete[] op_stack;
    op_stack = NULL;

    delete stack_pos;
    stack_pos = NULL;
  }
}

/********************************
 * Debugger main
 ********************************/
int main(int argc, char** argv) 
{
  Runtime::Debugger debugger;
  debugger.Debug();
}
