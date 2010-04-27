/***************************************************************************
 * Runtime debugger
 *
 * Copyright (c) 2010 Randy Hollines
 * All rights reserved.
 *
 * Reistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *tree.o scanner.o parser.o test.o
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

#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__

#include "../common.h"
#include "../interpreter.h"
#include "tree.h"
#include "parser.h"

using namespace std;

namespace Runtime {

  class StackInterpreter;

/********************************
 * Interactive command line
 * debugger
 ********************************/
class Debugger {
  StackInterpreter* interpreter;
  long* op_stack;
  long* stack_pos;
  
  list<int> breaks;
  
  int FindBreak(int l) {
    list<int>::iterator found = find(breaks.begin(), breaks.end(), l);
    if(found != breaks.begin()) {
      return *found;
    }

    return -1;
  }

  void AddBreak(int l) {
    if(l > -1 && FindBreak(l) < 0) {
      breaks.push_back(l);
    }
  }
  
  bool ProcessCommand(const string &line);
  void ProcessLoad(Load* load);
  void ProcessBreak(Break* break_command);
  void ProcessPrint(Print* print);
  void CleanProgram();
  
 public:
  void Debug() {
    cout << "-------------------------------------" << endl;
    cout << "Objeck v0.9.10 - Interactive Debugger" << endl;
    cout << "-------------------------------------" << endl;
    bool quit = false;
    do {
      // prompt for input
      cout << "> ";
      string line;
      getline(cin, line);
      quit = ProcessCommand(line);    
      cout << endl;
    } 
    while(!quit);
    cout << "Goodbye!" << endl;
  }
  
  Debugger();  
  ~Debugger();
  
  // runtime callback
  void ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack,
			  long call_stack_pos, StackFrame* frame);
};
}
#endif
