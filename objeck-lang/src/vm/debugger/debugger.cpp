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
    cout << "### file=" << file_name << ", line=" << line_num << " ###" << endl;
#endif

    if(line_num > -1 && (cur_line_num != line_num || cur_file_name != file_name)  && 
       (is_next || FindBreak(line_num, file_name))) {
      // set current line
      cur_line_num = line_num;
      cur_file_name = file_name;
      cur_frame = frame;
      cur_call_stack = call_stack;
      cur_call_stack_pos = call_stack_pos;
      
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
			 command->GetCommandType() != NEXT_COMMAND));
    }
  }
}

void Runtime::Debugger::ProcessLoad(Load* load) {
  if(FileExists(load->GetFileName(), true)) {
    ClearProgram();
    program_file = load->GetFileName();
    cout << "loaded program executable: file='" << program_file << "'" << endl;
  }
  else {
    cout << "program file doesn't exist." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessRun() {
  // make sure file exists
  if(program_file.size() > 0) {
    // TODO: pass args
    Loader loader(program_file.c_str()); 
    loader.Load();
    cur_program = loader.GetProgram();
    
    // execute
    op_stack = new long[STACK_SIZE];
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
    ClearProgram();
  }
  else {
    cout << "program file not specified." << endl;
  }
}

void Runtime::Debugger::ProcessBreak(FilePostion* break_command) {
  int line_num = break_command->GetLineNumber();
  string file_name = break_command->GetFileName();

  // TODO: fix path
  const string &path = base_path + file_name;  
  if(FileExists(path)) {  
    if(AddBreak(line_num, file_name)) {
      cout << "added break point: file='" << file_name << ":" << line_num << "'" << endl;
    }
    else {
      cout << "break point already exist." << endl;
    }
  }
  else {
    cout << "file doesn't exit." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessBreaks() {
  ListBreaks();
}

void Runtime::Debugger::ProcessDelete(FilePostion* delete_command) {
  int line_num = delete_command->GetLineNumber();
  string file_name = delete_command->GetFileName();
  
  // TODO fix
  const string &path = base_path + file_name;  
  if(FileExists(path)) {  
    if(DeleteBreak(line_num, file_name)) {
      cout << "deleted break point: file='" << file_name << ":" << line_num << "'" << endl;
    }
    else {
      cout << "break point doesn't exist." << endl;
    }
  }
  else {
    cout << "file doesn't exit." << endl;
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
	  cout << "print: type=Int[], value=" << reference->GetIntValue() 
	       << "(" << (void*)reference->GetIntValue() << ")";
	  if(reference->GetArrayDimension()) {
	    cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		 << reference->GetArraySize();
	  }
	  cout << endl;
	  break;

	case FLOAT_ARY_PARM:
	  cout << "print: type=Float[], value=" << reference->GetFloatValue() 
	       << "(" << (void*)reference->GetIntValue() << ")" << endl;
	  if(reference->GetArrayDimension()) {
	    cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		 << reference->GetArraySize();
	  }
	  cout << endl;
	  break;
	  
	case OBJ_PARM:
	  cout << "print: type=" << reference->GetClassName() << ", value=" 
	       << (void*)reference->GetIntValue() << endl;
	  break;
	  
	case OBJ_ARY_PARM:
	  cout << "print: type=" << reference->GetClassName() << "[], value=" 
	       << (void*)reference->GetIntValue();
	  if(reference->GetArrayDimension()) {
	    cout << ", dimension=" << reference->GetArrayDimension() << ", size="
		 << reference->GetArraySize();
	  }
	  cout << endl;
	  break;
	}
      }
      else {
	cout << "no program running." << endl;
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
  }
}

void Runtime::Debugger::EvaluateReference(Reference* reference, bool is_instance) {
  StackMethod* method = cur_frame->GetMethod();
  //
  // instance reference  
  //
  if(is_instance) {
    if(ref_mem && ref_klass) {
      // set declaration
      StackDclr dclr_value;
      int index = ref_klass->GetDeclaration(reference->GetVariableName(), dclr_value);
      reference->SetDeclaration(dclr_value);
      
      // check reference name
      if(index > -1) {
	switch(dclr_value.type) {	  
	case INT_PARM:
	  reference->SetIntValue(ref_mem[index]);
	  break;

	case FLOAT_PARM: {
	  FLOAT_VALUE value;
	  memcpy(&value, &ref_mem[index], sizeof(FLOAT_VALUE));
	  reference->SetFloatValue(value);
	}
	  break;

	case OBJ_PARM:
	  EvaluateObjectReference(reference, index, dclr_value.id);
	  break;
	  
	case BYTE_ARY_PARM:
	case INT_ARY_PARM:
	case OBJ_ARY_PARM:	  
	  EvaluateIntFloatReference(reference, index, false);
	  break;
	  
	case FLOAT_ARY_PARM:
	  EvaluateIntFloatReference(reference, index, true);
	  break;	
	}
      }
      else {
	cout << "unknown variable." << endl;
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
	dclr_value.id = method->GetClass()->GetId();
	reference->SetDeclaration(dclr_value);
	EvaluateObjectReference(reference, 0, method->GetClass()->GetId());
      }
      // process method reference
      else {
	// set declaration
	int index = method->GetDeclaration(reference->GetVariableName(), dclr_value);
	reference->SetDeclaration(dclr_value);
	
	// check reference name
	if(index > -1) {
	  switch(dclr_value.type) {	  
	  case INT_PARM:
	    reference->SetIntValue(ref_mem[index + 1]);
	    break;

	  case FLOAT_PARM: {
	    FLOAT_VALUE value;
	    memcpy(&value, &ref_mem[index + 1], sizeof(FLOAT_VALUE));
	    reference->SetFloatValue(value);
	  }
	    break;

	  case OBJ_PARM:
	    EvaluateObjectReference(reference, index + 1, dclr_value.id);
	    break;
	  
	  case BYTE_ARY_PARM:
	  case INT_ARY_PARM:
	  case OBJ_ARY_PARM:	  
	    EvaluateIntFloatReference(reference, index + 1, false);
	    break;
	  
	  case FLOAT_ARY_PARM:
	    EvaluateIntFloatReference(reference, index + 1, true);
	    break;	
	  }
	}
	else {
	  cout << "unknown variable." << endl;
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

void Runtime::Debugger::EvaluateObjectReference(Reference* reference, int index, int id) {
  reference->SetIntValue(ref_mem[index]);
  ref_mem = (long*)ref_mem[index];
  ref_klass = cur_program->GetClass(id);
  if(ref_klass->IsDebug()) {
    reference->SetClassName(ref_klass->GetName());
    if(reference->GetReference()) {
      EvaluateReference(reference->GetReference(), true);
    }
  }
  else {
    ref_klass = NULL;
    cout << "no debug information for class." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::EvaluateIntFloatReference(Reference* reference, int index, bool is_float) {
  long* array = (long*)ref_mem[index];    
  const int max = array[0];
  const int dim = array[1];
  
  // de-reference array value
  ExpressionList* indices = reference->GetIndices();
  if(indices) {
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
    reference->SetArrayDimension(dim);
    reference->SetArraySize(max);
    reference->SetIntValue(ref_mem[index]);
  }
}

Command* Runtime::Debugger::ProcessCommand(const string &line) {
#ifdef _DEBUG
  cout << "input: |" << line << "|" << endl;
#endif

  // parser input
  is_next = false;
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
	cout << "source file or line number doesn't exist." << endl;
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
      cout << "  are sure you want to clear all break points? [y/n] ";
      string line;
      getline(cin, line);      
      if(line == "y" || line == "yes") {
	cout << "break points cleared." << endl;
	ClearBreaks();
      }
    }
      break;

    case DELETE_COMMAND:
      ProcessDelete(static_cast<FilePostion*>(command));
      break;

    case NEXT_COMMAND:
      is_next = true;
      break;
      
    case INFO_COMMAND:
      ProcessInfo(static_cast<Info*>(command));
      break;
      
      /* TODO
	 case FRAME_COMMAND:
	 break;
      */
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
	  for(int i = 0; i < methods.size(); i++) {
	    StackMethod* method = methods[i];
	    // parse method name
	    int long_name_end = method->GetName().find_last_of(':');
	    const string &long_name = method->GetName().substr(0, long_name_end);
	    // print 
	    cout << "  class: type=" << klass->GetName() << ", method=" 
		 << long_name << "(..)" << endl;
	    cout << "  parameters:" << endl;
	    PrintDeclarations(method->GetDeclarations(), method->GetNumberDeclarations());
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
	PrintDeclarations(klass->GetDeclarations(), klass->GetNumberDeclarations());
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
    cout << "no program running." << endl;
  }
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
  is_error = false;
  ref_mem = NULL;
  ref_mem = NULL; 
}

void Runtime::Debugger::Debug() {
  cout << "-------------------------------------" << endl;
  cout << "Objeck v0.9.10 - Interactive Debugger" << endl;
  cout << "-------------------------------------" << endl << endl;

  if(FileExists(program_file, true) && FileExists(base_path)) {
    cout << "loaded executable: file='" << program_file << "'" << endl;
    cout << "source files: path='" << base_path << "'" << endl << endl;
  }
  else {
    cout << "unable to load executable or locate base path." << endl;
    exit(1);
  }
  
  // enter feedback loop
  Command* command;
  while(true) {
    cout << "> ";
    string line;
    getline(cin, line);
    command = ProcessCommand(line);    
  }
}

/********************************
 * Debugger main
 ********************************/
int main(int argc, char** argv) 
{
  string usage;
  usage += "Copyright (c) 2008-2010, Randy Hollines. All rights reserved.\n";
  usage += "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
  usage += "license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
  usage += "FOR MORE INFORMATION.\n\n";
  usage += "usage: obb -exe <executable> [-src <source>]\n\n";
  usage += "example: \"obc -src test_src\\prgm1.obs -dest prgm1.obe\"\n\n";
  usage += "options:\n";
  usage += "  -exe: executable file\n";
  usage += "  -src: source file path\n\n";
  usage += "  example: \"./obd -exe ../hello.obe -src .../\"";

  if(argc >= 3) {
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
      while((path[pos] == ' ' || path[pos] == '\t') && pos < end) {
	pos++;
      }
      if(path[pos] == '-') {
	// parse key
	int start =  ++pos;
	while(path[pos] != ' ' && path[pos] != '\t' && pos < end) {
	  pos++;
	}
	string key = path.substr(start, pos - start);
	// parse value
	while((path[pos] == ' ' || path[pos] == '\t') && pos < end) {
	  pos++;
	}
	start = pos;
	while(path[pos] != ' ' && path[pos] != '\t' && pos < end) {
	  pos++;
	}
	string value = path.substr(start, pos - start);
	arguments.insert(pair<string, string>(key, value));
      } 
      else {
	while((path[pos] == ' ' || path[pos] == '\t') && pos < end) {
	  pos++;
	}
	int start = pos;
	while(path[pos] != ' ' && path[pos] != '\t' && pos < end) {
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
    
    string base_path = "./";
    result = arguments.find("src");
    if(result != arguments.end()) {
      base_path = arguments["src"];
    }

    // go debugger
    Runtime::Debugger debugger(file_name, base_path);
    debugger.Debug();
    
    return 0;
  } 
  else {
    cerr << usage << endl << endl;
    return 1;
  }
  
  return 1;
}
