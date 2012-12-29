/**************************************************************************
 * Runtime debugger
 *
 * Copyright (c) 2010-2012 Randy Hollines
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
#include "../../shared/version.h"

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

    /*
      #ifdef _DEBUG
      cout << "### file=" << file_name << ", line=" << line_num << " ###" << endl;
      #endif
    */

    if(line_num > -1 && (cur_line_num != line_num || cur_file_name != file_name)  &&
       // step command
       (is_next || (is_jmp_out && call_stack_pos < cur_call_stack_pos) ||
	// next line
	(is_next_line && (cur_frame && frame->GetMethod()->GetName() == cur_frame->GetMethod()->GetName()) ||
	 (call_stack_pos < cur_call_stack_pos)) ||
	// break command
	FindBreak(line_num, file_name))) {
      // set current line
      cur_line_num = line_num;
      cur_file_name = file_name;
      cur_frame = frame;
      cur_call_stack = call_stack;
      cur_call_stack_pos = call_stack_pos;
      is_jmp_out = is_next_line = false;

      // prompt for input
      const string &long_name = cur_frame->GetMethod()->GetName();
      int end_index = long_name.find_last_of(':');
      const string &cls_mthd_name = long_name.substr(0, end_index);

      // show break info
      int mid_index = cls_mthd_name.find_last_of(':');
      const string &cls_name = cls_mthd_name.substr(0, mid_index);
      const string &mthd_name = cls_mthd_name.substr(mid_index + 1);
      cout << "break: file='" << file_name << ":" << line_num << "', method='"
	   << cls_name << "->" << mthd_name << "(..)'" << endl;

      // prompt for break command
      Command* command;
      do {
	cout << "> ";
	string line;
	getline(cin, line);
	command = ProcessCommand(line);
      }
      while(!command || (command->GetCommandType() != CONT_COMMAND &&
			 command->GetCommandType() != NEXT_COMMAND &&
			 command->GetCommandType() != NEXT_LINE_COMMAND &&
			 command->GetCommandType() != JUMP_OUT_COMMAND));
    }
  }
}

void Runtime::Debugger::ProcessSrc(Load* load) {
  if(interpreter) {
    cout << "unable to modify source path while program is running." << endl;
    return;
  }

  if(FileExists(program_file, true) && DirectoryExists(load->GetFileName())) {
    ClearProgram();
    base_path = load->GetFileName();
#ifdef _WIN32
    if(base_path.size() > 0 && base_path[base_path.size() - 1] != '\\') {
      base_path += '\\';
    }
#else
    if(base_path.size() > 0 && base_path[base_path.size() - 1] != '/') {
      base_path += '/';
    }
#endif
    cout << "source files: path='" << base_path << "'" << endl << endl;
  }
  else {
    cout << "unable to locate base path." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessArgs(Load* load) {
  // clear
  arguments.clear();
  arguments.push_back("obr");
  arguments.push_back(program_file);
  // parse arguments
  const char* temp = load->GetFileName().c_str();
  char* buffer = new char[load->GetFileName().size() + 1];
  strcpy(buffer, temp);
  char* token = strtok(buffer, " ");
  while(token) {
    arguments.push_back(token);
    token = strtok(NULL, " ");
  }
  cout << "program arguments sets." << endl;
  // clean up
  delete[] buffer;
  buffer = NULL;
}

void Runtime::Debugger::ProcessExe(Load* load) {
  if(interpreter) {
    cout << "unable to load executable while program is running." << endl;
    return;
  }

  if(FileExists(load->GetFileName(), true) && DirectoryExists(base_path)) {
    // clear program
    ClearProgram();
    ClearBreaks();
    program_file = load->GetFileName();
    // reset arguments
    arguments.clear();
    arguments.push_back("obr");
    arguments.push_back(program_file);
    cout << "loaded executable: file='" << program_file << "'" << endl;
  }
  else {
    cout << "program file doesn't exist." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessRun() {
  if(program_file.size() > 0) {
    // process program parameters
    const int argc = arguments.size();
    const char** argv = new const char*[argc];
    for(int i = 0; i < argc; i++) {
      argv[i] = arguments[i].c_str();
    }

    // envoke loader
    Loader loader(argc, argv);
    loader.Load();
    cur_program = loader.GetProgram();

    // execute
    op_stack = new long[CALC_STACK_SIZE];
    stack_pos = new long;
    (*stack_pos) = 0;

#ifdef _TIMING
    long start = clock();
#endif
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
    delete[] argv;
    argv = NULL;
    ClearProgram();
  }
  else {
    cout << "program file not specified." << endl;
  }
}

void Runtime::Debugger::ProcessBreak(FilePostion* break_command) {
  int line_num = break_command->GetLineNumber();
  if(line_num < 0) {
    line_num = cur_line_num;
  }
  
  string file_name = break_command->GetFileName();
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }
  
  const string &path = base_path + file_name;
  if(file_name.size() != 0 && FileExists(path)) {
    if(AddBreak(line_num, file_name)) {
      cout << "added breakpoint: file='" << file_name << ":" << line_num << "'" << endl;
    }
    else {
      cout << "breakpoint already exist." << endl;
    }
  }
  else {
    cout << "file doesn't exist or isn't loaded." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessBreaks() {
  if(breaks.size() > 0) {
    ListBreaks();
  }
  else {
    cout << "no breakpoints defined." << endl;
  }
}

void Runtime::Debugger::ProcessDelete(FilePostion* delete_command) {
  int line_num = delete_command->GetLineNumber();
  const string &file_name = delete_command->GetFileName();
  const string &path = base_path + file_name;

  if(FileExists(path)) {
    if(DeleteBreak(line_num, file_name)) {
      cout << "deleted breakpoint: file='" << file_name << ":" << line_num << "'" << endl;
    }
    else {
      cout << "breakpoint doesn't exist." << endl;
    }
  }
  else {
    cout << "file doesn't exist." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessPrint(Print* print) {
  Expression* expression = print->GetExpression();
  EvaluateExpression(expression);

  if(!is_error) {
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
	  cout << "print: type=Int, value=" << reference->GetIntValue() << endl;
	  break;

	case FUNC_PARM: {
	  StackClass* klass = cur_program->GetClass(reference->GetIntValue());
	  if(klass) {
	    cout << "print: type=Functon, class=" << klass->GetName() 
		 << ", method=" << PrintMethod(klass->GetMethod(reference->GetIntValue2())) << endl;
	  }
	}
	  break;
	  
	case FLOAT_PARM:
	  cout << "print: type=Float, value=" << reference->GetFloatValue() << endl;
	  break;

	case BYTE_ARY_PARM:
	  cout << "print: type=Byte[], value=" << (char)reference->GetIntValue()
	       << "(" << (void*)reference->GetIntValue() << ")";
	  if(reference->GetArrayDimension()) {
	    cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		 << reference->GetArraySize();
	  }
	  cout << endl;
	  break;

	case INT_ARY_PARM:
	  if(reference->GetIndices()) {
	    cout << "print: type=Int, value=" << reference->GetIntValue() << endl;
	  }
	  else {
	    cout << "print: type=Int[], value=" << reference->GetIntValue()
	         << "(" << (void*)reference->GetIntValue() << ")";
	    if(reference->GetArrayDimension()) {
	      cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		   << reference->GetArraySize();
	    }
	    cout << endl;
	  }
	  break;

	case FLOAT_ARY_PARM:
	  if(reference->GetIndices()) {
	    cout << "print: type=Float, value=" << reference->GetFloatValue() << endl;
	  }
	  else {
	    cout << "print: type=Float[], value=" << reference->GetFloatValue()
	         << "(" << (void*)reference->GetIntValue() << ")" << endl;
	    if(reference->GetArrayDimension()) {
	      cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		   << reference->GetArraySize();
	    }
	    cout << endl;
	  }
	  break;

	case OBJ_PARM:
	  if(ref_klass && ref_klass->GetName() == "System.String") {
	    long* instance = (long*)reference->GetIntValue();
	    if(instance) {
	      long* string_instance = (long*)instance[0];
	      const char* char_string = (char*)(string_instance + 3);
	      cout << "print: type=" << ref_klass->GetName() << ", value=\""
		   << char_string << "\"" << endl;
	    }
	    else {
	      cout << "print: type=" << (ref_klass ? ref_klass->GetName() : "System.Base") << ", value="
		   << (void*)reference->GetIntValue() << endl;
	    }
	  }
	  else {
	    cout << "print: type=" << (ref_klass ? ref_klass->GetName() : "System.Base") << ", value="
		 << (void*)reference->GetIntValue() << endl;
	  }
	  break;
	  
	case OBJ_ARY_PARM:
	  if(reference->GetIndices()) {
	    StackClass* klass = MemoryManager::Instance()->GetClass((long*)reference->GetIntValue());
	    if(klass) {	      
	      long* instance = (long*)reference->GetIntValue();
	      if(instance) {
		cout << "print: type=" << klass->GetName() << ", value=" << (void*)reference->GetIntValue() << endl;
	      }
	      else {
		cout << "print: type=System.Base, value=" << (void*)reference->GetIntValue() << endl;
	      }
	    }
	    else {
	      cout << "print: type=System.Base, value=" << (void*)reference->GetIntValue() << endl;
	    }
	  }
	  else {
	    cout << "print: type=System.Base[], value=" << (void*)reference->GetIntValue();
	    if(reference->GetArrayDimension()) {
	      cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		   << reference->GetArraySize();
	    }
	    cout << endl;
	  }
	  break;
	}
      }
      else {
	cout << "program is not running." << endl;
	is_error = true;
      }
      break;

    case NIL_LIT_EXPR:
      cout << "print: type=Nil, value=Nil" << endl;
      break;

    case CHAR_LIT_EXPR:
      cout << "print: type=Char, value=" << (char)expression->GetIntValue() << endl;
      break;

    case INT_LIT_EXPR:
      cout << "print: type=Int, value=" << expression->GetIntValue() << endl;
      break;

    case FLOAT_LIT_EXPR:
      cout << "print: type=Float, value=" << expression->GetFloatValue() << endl;
      break;

    case BOOLEAN_LIT_EXPR:
      cout << "print: type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << endl;
      break;

    case AND_EXPR:
    case OR_EXPR:
    case EQL_EXPR:
    case NEQL_EXPR:
    case LES_EXPR:
    case GTR_EQL_EXPR:
    case LES_EQL_EXPR:
    case GTR_EXPR:
      cout << "print: type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << endl;
      break;

    case ADD_EXPR:
    case SUB_EXPR:
    case MUL_EXPR:
    case DIV_EXPR:
    case MOD_EXPR:
      if(expression->GetFloatEval()) {
	cout << "print: type=Float, value=" << expression->GetFloatValue() << endl;
      }
      else {
	cout << "print: type=Int, value=" << expression->GetIntValue() << endl;
      }
      break;

    case CHAR_STR_EXPR:
      break;
    }
  }
}

void Runtime::Debugger::EvaluateExpression(Expression* expression) {
  switch(expression->GetExpressionType()) {
  case REF_EXPR:
    if(interpreter) {
      EvaluateReference(static_cast<Reference*>(expression), false);
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
    if(left->GetIntValue() && right->GetIntValue()) {
      expression->SetIntValue(left->GetIntValue() % right->GetIntValue());
    }
    else {
      cout << "modulus operation requires integer values." << endl;
      is_error = true;
    }
    break;
      
  default:
    break;
  }
}

void Runtime::Debugger::EvaluateReference(Reference* reference, bool is_instance) {
  StackMethod* method = cur_frame->GetMethod();
  //
  // instance reference
  //
  if(is_instance) {
    if(ref_mem && ref_klass) {
      // check reference name
      StackDclr dclr_value;
      bool found = ref_klass->GetDeclaration(reference->GetVariableName(), dclr_value);
      if(found) {
	reference->SetDeclaration(dclr_value);
	
	switch(dclr_value.type) {
	case INT_PARM:
	  reference->SetIntValue(ref_mem[dclr_value.id]);
	  break;

	case FUNC_PARM:
	  reference->SetIntValue(ref_mem[dclr_value.id]);
	  reference->SetIntValue2(ref_mem[dclr_value.id + 1]);
	  break;

	case FLOAT_PARM: {
	  FLOAT_VALUE value;
	  memcpy(&value, &ref_mem[dclr_value.id], sizeof(FLOAT_VALUE));
	  reference->SetFloatValue(value);
	}
	  break;

	case OBJ_PARM:
	  EvaluateObjectReference(reference, dclr_value.id);
	  break;

	case BYTE_ARY_PARM:
	  EvaluateByteReference(reference, dclr_value.id);
	  break;
	  
	case INT_ARY_PARM:
	  EvaluateIntFloatReference(reference, dclr_value.id, false);
	  break;

	case OBJ_ARY_PARM:
	  EvaluateIntFloatReference(reference, dclr_value.id, false);
	  break;

	case FLOAT_ARY_PARM:
	  EvaluateIntFloatReference(reference, dclr_value.id, true);
	  break;
	}
      }
      else {
	cout << "unknown variable (or no debug information for class)." << endl;
	is_error = true;
      }
    }
    else {
      cout << "unable to deference empty frame." << endl;
      is_error = true;
    }
  }
  //
  // method reference
  //
  else {
    ref_mem = cur_frame->GetMemory();
    if(ref_mem) {
      StackDclr dclr_value;

      // process check self
      if(reference->IsSelf()) {
	dclr_value.name = "@self";
	dclr_value.type = OBJ_PARM;
	reference->SetDeclaration(dclr_value);
	EvaluateObjectReference(reference, 0);
      }
      // process method reference
      else {
	// check reference name
	bool found = method->GetDeclaration(reference->GetVariableName(), dclr_value);
	reference->SetDeclaration(dclr_value);
	if(found) {
	  if(method->HasAndOr()) {
	    dclr_value.id++;
	  }
	  
	  switch(dclr_value.type) {
	  case INT_PARM:
	    reference->SetIntValue(ref_mem[dclr_value.id + 1]);
	    break;

	  case FUNC_PARM:
	    reference->SetIntValue(ref_mem[dclr_value.id + 1]);
	    reference->SetIntValue2(ref_mem[dclr_value.id + 2]);
	    break;

	  case FLOAT_PARM: {
	    FLOAT_VALUE value;
	    memcpy(&value, &ref_mem[dclr_value.id + 1], sizeof(FLOAT_VALUE));
	    reference->SetFloatValue(value);
	  }
	    break;

	  case OBJ_PARM:
	    EvaluateObjectReference(reference, dclr_value.id + 1);
	    break;

	  case BYTE_ARY_PARM:
	    EvaluateByteReference(reference, dclr_value.id + 1);
	    break;
	    
	  case INT_ARY_PARM:
	    EvaluateIntFloatReference(reference, dclr_value.id + 1, false);
	    break;

	  case OBJ_ARY_PARM:
	    EvaluateIntFloatReference(reference, dclr_value.id + 1, false);
	    break;

	  case FLOAT_ARY_PARM:
	    EvaluateIntFloatReference(reference, dclr_value.id + 1, true);
	    break;
	  }
	}
	else {
	  cout << "unknown variable (or no debug information for class)." << endl;
	  is_error = true;
	}
      }
    }
    else {
      cout << "unable to deference empty frame." << endl;
      is_error = true;
    }
  }
}

void Runtime::Debugger::EvaluateObjectReference(Reference* reference, int index) {
  if(ref_mem) {
    reference->SetIntValue(ref_mem[index]);
    ref_mem = (long*)ref_mem[index];
    ref_klass = MemoryManager::GetClass(ref_mem);
    if(reference->GetReference()) {
      EvaluateReference(reference->GetReference(), true);
    }
  }
  else {
    cout << "current object reference is Nil" << endl;
    is_error = true;
  }
}

void Runtime::Debugger::EvaluateByteReference(Reference* reference, int index) {
  long* array = (long*)ref_mem[index];
  if(array) {
    const int max = array[0];
    const int dim = array[1];

    // de-reference array value
    ExpressionList* indices = reference->GetIndices();
    if(indices) {
      // calculate indices values
      vector<Expression*> expressions = indices->GetExpressions();
      vector<int> values;
      for(size_t i = 0; i < expressions.size(); i++) {
	EvaluateExpression(expressions[i]);
	// update values
	if(expressions[i]->GetFloatEval()) {
	  values.push_back((int)expressions[i]->GetFloatValue());
	}
	else {
	  values.push_back(expressions[i]->GetIntValue());
	}
      }
      // match the dimensions
      if(expressions.size() == (size_t)dim) {
	// calculate indices
	array += 2;
	int j = dim - 1;
	long array_index = values[j--];
	for(long i = 1; i < dim; i++) {
	  array_index *= array[i];
	  array_index += values[j--];
	}
	array += dim;
	
	if(array_index > -1 && array_index < max) {
	  reference->SetIntValue(((char*)array)[array_index]);
	}
	else {
	  cout << "array index out of bounds." << endl;
	  is_error = true;
	}
      }
      else {
	cout << "array dimension mismatch." << endl;
	is_error = true;
      }
    }
    // set array address
    else {
      if(ref_mem) {
	reference->SetArrayDimension(dim);
	reference->SetArraySize(max);
	reference->SetIntValue(ref_mem[index]);
      }
      else {
	cout << "current reference is Nil" << endl;
	is_error = true;
      }
    }
  }
  else {
    cout << "current array value is Nil" << endl;
    is_error = true;
  }
}

void Runtime::Debugger::EvaluateIntFloatReference(Reference* reference, int index, bool is_float) {
  long* array = (long*)ref_mem[index];
  if(array) {
    const int max = array[0];
    const int dim = array[1];

    // de-reference array value
    ExpressionList* indices = reference->GetIndices();
    if(indices) {
      // calculate indices values
      vector<Expression*> expressions = indices->GetExpressions();
      vector<int> values;
      for(size_t i = 0; i < expressions.size(); i++) {
	EvaluateExpression(expressions[i]);
	// update values
	if(expressions[i]->GetFloatEval()) {
	  values.push_back((int)expressions[i]->GetFloatValue());
	}
	else {
	  values.push_back(expressions[i]->GetIntValue());
	}
      }
      // match the dimensions
      if(expressions.size() == (size_t)dim) {
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
	    cout << "array index out of bounds." << endl;
	    is_error = true;
	  }
	}
	// check int array bounds
	else {
	  if(array_index > -1 && array_index < max) {
	    reference->SetIntValue(array[array_index]);
	  }
	  else {
	    cout << "array index out of bounds." << endl;
	    is_error = true;
	  }
	}
      }
      else {
	cout << "array dimension mismatch." << endl;
	is_error = true;
      }
    }
    // set array address
    else {
      if(ref_mem) {
	reference->SetArrayDimension(dim);
	reference->SetArraySize(max);
	reference->SetIntValue(ref_mem[index]);
      }
      else {
	cout << "current reference is Nil" << endl;
	is_error = true;
      }
    }
  }
  else {
    cout << "current array value is Nil" << endl;
    is_error = true;
  }
}

Command* Runtime::Debugger::ProcessCommand(const string &line) {
#ifdef _DEBUG
  cout << "input: |" << line << "|" << endl;
#endif

  // parser input
  is_next = is_next_line = false;
  Parser parser;
  Command* command = parser.Parse("?" + line);
  if(command) {
    switch(command->GetCommandType()) {
    case EXE_COMMAND:
      ProcessExe(static_cast<Load*>(command));
      break;

    case SRC_COMMAND:
      ProcessSrc(static_cast<Load*>(command));
      break;

    case ARGS_COMMAND:
      ProcessArgs(static_cast<Load*>(command));
      break;

    case QUIT_COMMAND:
      ClearBreaks();
      ClearProgram();
      cout << "goodbye." << endl;
      exit(0);
      break;

    case LIST_COMMAND: {
      FilePostion* file_pos = static_cast<FilePostion*>(command);

      string file_name;
      if(file_pos->GetFileName().size() > 0) {
	file_name = file_pos->GetFileName();
      }
      else {
	file_name = cur_file_name;
      }

      int line_num;
      if(file_pos->GetLineNumber() > 0) {
	line_num = file_pos->GetLineNumber();
      }
      else {
	line_num = cur_line_num;
      }

      const string &path = base_path + file_name;
      if(FileExists(path) && line_num > 0) {
	SourceFile src_file(path, cur_line_num);
	if(!src_file.Print(line_num)) {
	  cout << "invalid line number." << endl;
	  is_error = true;
	}
      }
      else {
	cout << "source file or line number doesn't exist. (is the program running?)" << endl;
	is_error = true;
      }
    }
      break;

    case BREAK_COMMAND:
      ProcessBreak(static_cast<FilePostion*>(command));
      break;

    case BREAKS_COMMAND:
      ProcessBreaks();
      break;

    case PRINT_COMMAND:
      ProcessPrint(static_cast<Print*>(command));
      break;

    case RUN_COMMAND:
      ProcessRun();
      break;

    case CLEAR_COMMAND: {
      cout << "  are sure you want to clear all breakpoints? [y/n] ";
      string line;
      getline(cin, line);
      if(line == "y" || line == "yes") {
	ClearBreaks();
      }
    }
      break;

    case DELETE_COMMAND:
      ProcessDelete(static_cast<FilePostion*>(command));
      break;

    case NEXT_COMMAND:
      if(interpreter) {
	is_next = true;
      }
      else {
	cout << "program is not running." << endl;
      }
      break;

    case NEXT_LINE_COMMAND:
      if(interpreter) {
	is_next_line = true;
      }
      else {
	cout << "program is not running." << endl;
      }
      break;

    case JUMP_OUT_COMMAND:
      if(interpreter) {
	is_jmp_out = true;
      }
      else {
	cout << "program is not running." << endl;
      }
      break;

    case CONT_COMMAND:
      if(!interpreter) {
	cout << "program is not running." << endl;
      }
      break;

    case INFO_COMMAND:
      ProcessInfo(static_cast<Info*>(command));
      break;

    case STACK_COMMAND:
      if(interpreter) {
	long pos = cur_call_stack_pos;
	cout << "stack:" << endl;
	do {
	  StackMethod* method = cur_call_stack[pos]->GetMethod();
	  cerr << "  frame: pos=" << pos << ", class=" << method->GetClass()->GetName() 
	       << ", method=" << PrintMethod(method);
	  const long ip = cur_call_stack[pos]->GetIp();
	  if(ip > 0) {
	    StackInstr* instr = cur_call_stack[pos]->GetMethod()->GetInstruction(ip);
	    cerr << ", file=" << method->GetClass()->GetFileName() << ":" << instr->GetLineNumber() << endl;
	  }
	  else {
	    cerr << endl;
	  }
	}
	while(--pos);
      }
      else {
	cout << "program is not running." << endl;
      }
      break;
	
    default:
      break;
    }

    is_error = false;
    ref_mem = NULL;
    return command;
  }
  else {
    cout << "unable to process command." << endl;
  }

  is_error = false;
  ref_mem = NULL;
  return NULL;
}

void Runtime::Debugger::ProcessInfo(Info* info) {
  const string &cls_name = info->GetClassName();
  const string &mthd_name = info->GetMethodName();

#ifdef _DEBUG
  cout << "--- info class=" << cls_name << ", method=" << mthd_name << " ---" << endl;
#endif

  if(interpreter) {
    // method info
    if(cls_name.size() > 0 && mthd_name.size() > 0) {
      StackClass* klass = cur_program->GetClass(cls_name);
      if(klass && klass->IsDebug()) {
	vector<StackMethod*> methods = klass->GetMethods(mthd_name);
	if(methods.size() > 0) {
	  for(size_t i = 0; i < methods.size(); i++) {
	    StackMethod* method = methods[i];
	    cout << "  class: type=" << klass->GetName() << ", method="
		 << PrintMethod(method) << endl;
	    cout << "  parameters:" << endl;
	    PrintDeclarations(method->GetDeclarations(), method->GetNumberDeclarations(), cls_name);
	  }
	}
	else {
	  cout << "unable to find method." << endl;
	  is_error = true;
	}
      }
      else {
	cout << "unable to find class." << endl;
	is_error = true;
      }
    }
    // class info
    else if(cls_name.size() > 0) {
      StackClass* klass = cur_program->GetClass(cls_name);
      if(klass && klass->IsDebug()) {
	cout << "  class: type=" << klass->GetName() << endl;
	// print
	cout << "  parameters:" << endl;
	PrintDeclarations(klass->GetInstanceDeclarations(), klass->GetNumberInstanceDeclarations(), klass->GetName());
      }
      else {
	cout << "unable to find class." << endl;
	is_error = true;
      }
    }
    // general info
    else {
      cout << "general info:" << endl;
      cout << "  program executable: file='" << program_file << "'" << endl;

      // parse method and class names
      const string &long_name = cur_frame->GetMethod()->GetName();
      int end_index = long_name.find_last_of(':');
      const string &cls_mthd_name = long_name.substr(0, end_index);

      int mid_index = cls_mthd_name.find_last_of(':');
      const string &cls_name = cls_mthd_name.substr(0, mid_index);
      const string &mthd_name = cls_mthd_name.substr(mid_index + 1);

      // print
      cout << "  current file='" << cur_file_name << ":" << cur_line_num << "', method='"
	   << cls_name << "->" << mthd_name << "(..)'" << endl;
    }
  }
  else {
    cout << "program is not running." << endl;
  }
}

void Runtime::Debugger::ClearBreaks() {
  cout << "breakpoints cleared." << endl;
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

  /* TODO: fix
     while(cur_call_stack_pos) {
     StackFrame* frame = cur_call_stack[--cur_call_stack_pos];
     delete frame;
     frame = NULL;
     }
     cur_call_stack = NULL;
  */

  MemoryManager::Instance()->Clear();

  cur_line_num = -2;
  cur_frame = NULL;
  cur_program = NULL;
  is_error = false;
  ref_mem = NULL;
  ref_mem = NULL;
  is_jmp_out = false;
}

void Runtime::Debugger::Debug() {
  cout << "-------------------------------------" << endl;
  cout << "Objeck " << VERSION_STRING << " - Interactive Debugger" << endl;
  cout << "-------------------------------------" << endl << endl;

  if(FileExists(program_file, true) && DirectoryExists(base_path)) {
    cout << "loaded executable: file='" << program_file << "'" << endl;
    cout << "source files: path='" << base_path << "'" << endl << endl;
    // clear arguments
    arguments.clear();
    arguments.push_back("obr");
    arguments.push_back(program_file);
  }
  else {
    cerr << "unable to load executable or locate base path." << endl;
    exit(1);
  }

  // enter feedback loop
  while(true) {
    cout << "> ";
    string line;
    getline(cin, line);
    ProcessCommand(line);
  }
}

/********************************
 * Debugger main
 ********************************/
int main(int argc, char** argv)
{
  string usage;
  usage += "Copyright (c) 2010-2012, Randy Hollines. All rights reserved.\n";
  usage += "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
  usage += "license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
  usage += "FOR MORE INFORMATION.\n\n";
  usage += VERSION_STRING;
  usage += "\n\n";
  usage += "usage: obd -exe <executable> [-src <source directory>]\n";
  usage += "example: \"obd -exe test_src\\prgm1.obe -src test_src\"\n\n";
  usage += "options:\n";
  usage += "  -exe: executable file\n";
  usage += "  -src: source directory path";

  if(argc >= 3) {
    // Initialize OpenSSL
    CRYPTO_malloc_init();
    SSL_library_init();

#ifdef _WIN32
	WSADATA data;
    int version = MAKEWORD(2, 2);
#endif

    // reconstruct path
    string path;
    for(int i = 1; i < argc; i++) {
      path += " ";
      path += argv[i];
    }

    // parse path
    int end = (int)path.size();
    map<const string, string> arguments;
    int pos = 0;
    while(pos < end) {
      // ignore leading white space
      while( pos < end && (path[pos] == ' ' || path[pos] == '\t')) {
	pos++;
      }
      if(path[pos] == '-') {
	// parse key
	int start =  ++pos;
	while( pos < end && path[pos] != ' ' && path[pos] != '\t') {
	  pos++;
	}
	string key = path.substr(start, pos - start);
	// parse value
	while(pos < end && (path[pos] == ' ' || path[pos] == '\t')) {
	  pos++;
	}
	start = pos;
	bool is_string = false;
	if(pos < end && path[pos] == '\'') {
	  is_string = true;
	  start++;
	}
	bool not_end = true;
	while(pos < end && not_end) {
	  // check for end
	  if(is_string) {
	    not_end = path[pos] != '\'';
	  }
	  else {
	    not_end = path[pos] != ' ' && path[pos] != '\t';
	  }
	  // update position
	  if(not_end) {
	    pos++;
	  }
	}
	string value;
	if(is_string) {
	  value = path.substr(start, pos - start - 1);
	}
	else {
	  value = path.substr(start, pos - start);
	}
	arguments.insert(pair<string, string>(key, value));
      }
      else {
	while(pos < end && (path[pos] == ' ' || path[pos] == '\t')) {
	  pos++;
	}
	int start = pos;
	while(pos < end && path[pos] != ' ' && path[pos] != '\t') {
	  pos++;
	}
	string value = path.substr(start, pos - start);
	arguments.insert(pair<string, string>("-", value));
      }
    }
    // start debugger
    map<const string, string>::iterator result = arguments.find("exe");
    if(result == arguments.end()) {
      cerr << usage << endl << endl;
      return 1;
    }
    const string &file_name = arguments["exe"];

    string base_path = ".";
    result = arguments.find("src");
    if(result != arguments.end()) {
      base_path = arguments["src"];
    }

#ifdef _WIN32
    if(base_path.size() > 0 && base_path[base_path.size() - 1] != '\\') {
      base_path += '\\';
    }
#else
    if(base_path.size() > 0 && base_path[base_path.size() - 1] != '/') {
      base_path += '/';
    }
#endif

    // go debugger
    Runtime::Debugger debugger(file_name, base_path);
    debugger.Debug();
#ifdef _WIN32
	WSACleanup();
#endif

    return 0;
  }
  else {
#ifdef _WIN32
	WSACleanup();
#endif
    cerr << usage << endl << endl;
    return 1;
  }

  return 1;
}
