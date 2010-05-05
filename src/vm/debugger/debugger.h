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

  typedef struct _UserBreak {
    int line_num;
    string file_name;
  } UserBreak;

  /********************************
   * Source file
   ********************************/
  class SourceFile {
    string file_name;
    vector<string> lines;
    
  public:
    SourceFile(const string &fn) {
      file_name = fn;

      ifstream file_in (fn.c_str());
      while(file_in.good()) {
	string line;
	getline(file_in, line);
	lines.push_back(line);
      }
      file_in.close();
    }

    ~SourceFile() {
    }

    bool IsLoaded() {
      return lines.size() > 0;
    }
    
    bool Print(int start) {
      return Print(start, start + 10);
    }
    
    bool Print(int start, int end) {
      start--;
      end--;
      
      if(start < 0 || start >= end || start >= lines.size()) {
	return false;
      }

      for(int i = start; i < lines.size() && i < end; i++) {
	cout << (i + 1) << ": " << lines[i] << endl;
      }
      
      return true;
    }

    const string& GetFileName() {
      return file_name;
    }
  };
  
  /********************************
   * Interactive command line
   * debugger
   ********************************/
  class Debugger {
    bool quit;
    string program_file;
    // break info
    list<UserBreak*> breaks;
    int cur_line_num;
    string cur_file_name;
    StackFrame** cur_call_stack;    
    long cur_call_stack_pos; 
    // interpreter variables
    StackInterpreter* interpreter;
    StackProgram* cur_program;
    StackFrame* cur_frame;
    long* op_stack;
    long* stack_pos;
    bool is_error;
    bool is_next;
    long* ref_mem;
    StackClass* ref_klass;
    
    bool FileExists(const string &file_name, bool is_exe = false) {
      ifstream touch(file_name.c_str(), ios::binary);
      if(touch.is_open()) {	
	if(is_exe) {
	  int magic_num;
	  touch.read((char*)&magic_num, sizeof(int));
	  touch.close();	  	  
	  return magic_num == 0xdddd;
	}
	touch.close();	
	return true;
      }
      
      return false;
    }
    
    bool DeleteBreak(int line_num, const string &file_name) {      
      UserBreak* user_break = FindBreak(line_num, file_name);
      if(user_break) {
	breaks.remove(user_break);
	return true;
      }

      return false;
    }
    
    UserBreak* FindBreak(int line_num, const string &file_name) {      
      for(list<UserBreak*>::iterator iter = breaks.begin(); iter != breaks.end(); iter++) {
	UserBreak* user_break = (*iter);
	if(user_break->line_num == line_num && user_break->file_name == file_name) {
	  return *iter;
	}
      }
      
      return NULL;
    }

    bool AddBreak(int line_num, const string &file_name) {
      if(!FindBreak(line_num, file_name)) {
	UserBreak* user_break = new UserBreak;
	user_break->line_num = line_num;
	user_break->file_name = file_name;
	breaks.push_back(user_break);
	return true;
      }

      return false;
    }
    
    Command* ProcessCommand(const string &line);
    void ProcessRun();
    void ProcessLoad(Load* load);
    void ProcessBreak(FilePostion* break_command);
    void ProcessDelete(FilePostion* break_command);
    void ProcessList(FilePostion* break_command);
    void ProcessPrint(Print* print);
    void ClearProgram();
    void ClearBreaks();

    void EvaluateExpression(Expression* expression);
    void EvaluateReference(Reference* reference, bool is_instance);
    void EvaluateObjectReference(Reference* reference, int index, int id);
    void EvaluateIntFloatReference(Reference* reference, int index, bool is_float);
    void EvaluateCalculation(CalculatedExpression* expression);
  
  public:
    void Debug() {
      cout << "-------------------------------------" << endl;
      cout << "Objeck v0.9.10 - Interactive Debugger" << endl;
      cout << "-------------------------------------" << endl << endl;

      if(program_file.size() > 0 && FileExists(program_file, true)) {
	cout << "loaded program executable: file='" << program_file << "'" << endl;
      }
      // enter feedback look
      Command* command;
      while(true) {
	cout << "> ";
	string line;
	getline(cin, line);
	command = ProcessCommand(line);    
      }
    }
  
    Debugger(const string &fn);  
    ~Debugger();
  
    // runtime callback
    void ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack,
			    long call_stack_pos, StackFrame* frame);
  };
}
#endif
