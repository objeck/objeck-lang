/**************************************************************************
 * Runtime debugger
 *
 * Copyright (c) 2010-2013 Randy Hollines
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
 * - Neither the name of the Objeck team nor the names of its
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
#include "../shared/sys.h"
#include "../shared/version.h"

/********************************
 * Interactive command line
 * debugger
 ********************************/
void Runtime::Debugger::ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack,
                                           long call_stack_pos, StackFrame* frame)
{
  if(frame->method->GetClass()) {
    const int line_num = instr->GetLineNumber();
    const wstring &file_name = frame->method->GetClass()->GetFileName();

    // wcout << L"### file=" << file_name << L", line=" << line_num << L" ###" << endl;
    
    if((line_num > -1 && (cur_line_num != line_num || cur_file_name != file_name)) &&
       // break point
       (FindBreak(line_num, file_name) ||
        // step command
        (is_next || (is_jmp_out && call_stack_pos < cur_call_stack_pos)) ||
        // next line
        (is_next_line && ((cur_frame && frame->method == cur_frame->method) ||
													(call_stack_pos < cur_call_stack_pos))))) {
      // set current line
      cur_line_num = line_num;
      cur_file_name = file_name;
      cur_frame = frame;
      cur_call_stack = call_stack;
      cur_call_stack_pos = call_stack_pos;
      is_jmp_out = is_next_line = false;

      // prompt for input
      const wstring &long_name = cur_frame->method->GetName();
      size_t end_index = long_name.find_last_of(':');
      const wstring &cls_mthd_name = long_name.substr(0, end_index);

      // show break info
      size_t mid_index = cls_mthd_name.find_last_of(':');
      const wstring &cls_name = cls_mthd_name.substr(0, mid_index);
      const wstring &mthd_name = cls_mthd_name.substr(mid_index + 1);
      wcout << L"break: file='" << file_name << L":" << line_num << L"', method='"
            << cls_name << L"->" << mthd_name << L"(..)'" << endl;

      // prompt for break command
      Command* command;
      wcout << L"> ";
      do {
				wstring line;
				getline(wcin, line);
				if(line.size() > 0) {
					command = ProcessCommand(line);
					wcout << L"> ";
				}
				else {
					command = NULL;
				}
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
    wcout << L"unable to modify source path while program is running." << endl;
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
    wcout << L"source files: path='" << base_path << L"'" << endl << endl;
  }
  else {
    wcout << L"unable to locate base path." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessArgs(Load* load) {
  // clear
  arguments.clear();
  arguments.push_back(L"obr");
  arguments.push_back(program_file);
  // parse arguments
  const wstring temp = load->GetFileName();
  const size_t buffer_max = temp.size() + 1;
  wchar_t* buffer = (wchar_t*)calloc(sizeof(wchar_t), buffer_max);
#ifdef _WIN32
  wcsncpy_s(buffer, buffer_max, temp.c_str(), temp.size());
#else
  wcsncpy(buffer, temp.c_str(), temp.size());
#endif

  wchar_t *state;
#ifdef _WIN32
  wchar_t* token = wcstok_s(buffer, L" ", &state);
#else
  wchar_t* token = wcstok(buffer, L" ", &state);
#endif
  while(token) {
    arguments.push_back(token);
#ifdef _WIN32
    token = wcstok_s(NULL, L" ", &state);
#else
    token = wcstok(NULL, L" ", &state);
#endif
  }
  wcout << L"program arguments sets." << endl;

  // clean up
  free(buffer);
  buffer = NULL;
}

void Runtime::Debugger::ProcessExe(Load* load) {
  if(interpreter) {
    wcout << L"unable to load executable while program is running." << endl;
    return;
  }

  if(FileExists(load->GetFileName(), true) && DirectoryExists(base_path)) {
    // clear program
    ClearProgram();
    ClearBreaks();
    program_file = load->GetFileName();
    // reset arguments
    arguments.clear();
    arguments.push_back(L"obr");
    arguments.push_back(program_file);
    wcout << L"loaded executable: file='" << program_file << L"'" << endl;
  }
  else {
    wcout << L"program file doesn't exist." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessRun() {
  if(program_file.size() > 0) {
    // process program parameters
    long argc = (long)arguments.size();
    wchar_t** argv = new wchar_t*[argc];
    for(long i = 0; i < argc; ++i) {
#ifdef _WIN32
      argv[i] = _wcsdup(arguments[i].c_str());
#else
      argv[i] = wcsdup(arguments[i].c_str());
#endif
    }

    // envoke loader
    Loader loader(argc, argv);
    loader.Load();
    cur_program = loader.GetProgram();

    // execute
    op_stack = new size_t[CALC_STACK_SIZE];
    stack_pos = new long;
    (*stack_pos) = 0;

#ifdef _TIMING
    long start = clock();
#endif
    interpreter = new Runtime::StackInterpreter(cur_program, this);
    interpreter->Execute(op_stack, stack_pos, 0, cur_program->GetInitializationMethod(), NULL, false);
#ifdef _TIMING
    wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << endl;
    wcout << L"---------------------------" << endl;
    wcout << L"Time: " << (float)(clock() - start) / CLOCKS_PER_SEC
					<< L" second(s)." << endl;
#endif

#ifdef _DEBUG
    wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << endl;
#endif

    // clear old program
    for(int i = 0; i < argc; i++) {
      wchar_t* param = argv[i];
      free(param);
      param = NULL;
    }
    delete[] argv;
    argv = NULL;
    ClearProgram();
  }
  else {
    wcout << L"program file not specified." << endl;
  }
}

void Runtime::Debugger::ProcessBreak(FilePostion* break_command) {
  int line_num = break_command->GetLineNumber();
  if(line_num < 0) {
    line_num = cur_line_num;
  }

  wstring file_name = break_command->GetFileName();
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }

  const wstring &path = base_path + file_name;
  if(file_name.size() != 0 && FileExists(path)) {
    if(AddBreak(line_num, file_name)) {
      wcout << L"added breakpoint: file='" << file_name << L":" << line_num << L"'" << endl;
    }
    else {
      wcout << L"breakpoint already exist." << endl;
    }
  }
  else {
    wcout << L"file doesn't exist or isn't loaded." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessBreaks() {
  if(breaks.size() > 0) {
    ListBreaks();
  }
  else {
    wcout << L"no breakpoints defined." << endl;
  }
}

void Runtime::Debugger::ProcessDelete(FilePostion* delete_command) {
  int line_num = delete_command->GetLineNumber();
  if(line_num < 0) {
    line_num = cur_line_num;
  }

  wstring file_name = delete_command->GetFileName();
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }

  const wstring &path = base_path + file_name;
  if(file_name.size() != 0 && FileExists(path)) {
    if(DeleteBreak(line_num, file_name)) {
      wcout << L"removed breakpoint: file='" << file_name << L":" << line_num << L"'" << endl;
    }
    else {
      wcout << L"breakpoint doesn't exist." << endl;
    }
  }
  else {
    wcout << L"file doesn't exist or isn't loaded." << endl;
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
        case CHAR_PARM:
          if(reference->GetIndices()) {
            wcout << L"cannot reference scalar variable" << endl;
          }
          else {
            wcout << L"print: type=Char, value=" << (wchar_t)reference->GetIntValue() << endl;
          }
          break;

        case INT_PARM:
          if(reference->GetIndices()) {
            wcout << L"cannot reference scalar variable" << endl;
          }
          else {
            wcout << L"print: type=Int, value=" << (long)reference->GetIntValue() << endl;
          }
          break;


        case FLOAT_PARM:
          if(reference->GetIndices()) {
            wcout << L"cannot reference scalar variable" << endl;
          }
          else {
            wcout << L"print: type=Float, value=" << reference->GetFloatValue() << endl;
          }
          break;

        case BYTE_ARY_PARM:
          if(reference->GetIndices()) {
            wcout << L"print: type=Int, value=" << (unsigned char)reference->GetIntValue() << endl;
          }
          else {
            wcout << L"print: type=Byte[], value=" << reference->GetIntValue()
									<< L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size="
										<< reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case CHAR_ARY_PARM:
          if(reference->GetIndices()) {
            wcout << L"print: type=Char, value=" << (wchar_t)reference->GetIntValue() << endl;
          }
          else {
            wcout << L"print: type=Char[], value=" << reference->GetIntValue()
									<< L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size="
										<< reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case INT_ARY_PARM:
          if(reference->GetIndices()) {
            wcout << L"print: type=Int, value=" << reference->GetIntValue() << endl;
          }
          else {
            wcout << L"print: type=Int[], value=" << reference->GetIntValue()
									<< L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size="
										<< reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case FLOAT_ARY_PARM:
          if(reference->GetIndices()) {
            wcout << L"print: type=Float, value=" << reference->GetFloatValue() << endl;
          }
          else {
            wcout << L"print: type=Float[], value=" << reference->GetIntValue()
									<< L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size="
										<< reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case OBJ_PARM:
          if(ref_klass && ref_klass->GetName() == L"System.String") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              size_t* string_instance = (size_t*)instance[0];
              const wchar_t* char_string = (wchar_t*)(string_instance + 3);
              wcout << L"print: type=" << ref_klass->GetName() << L", value=\""
										<< char_string << L"\"" << endl;
            }
            else {
              wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value="
										<< (void*)reference->GetIntValue() << endl;
            }
          }
          else {
            wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value="
									<< (void*)reference->GetIntValue() << endl;
          }
          break;

        case OBJ_ARY_PARM:
          if(reference->GetIndices()) {
            StackClass* klass = MemoryManager::GetClass((size_t*)reference->GetIntValue());
            if(klass) {	      
              size_t* instance = (size_t*)reference->GetIntValue();
              if(instance) {
                wcout << L"print: type=" << klass->GetName() << L", value=" << (void*)reference->GetIntValue() << endl;
              }
              else {
                wcout << L"print: type=System.Base, value=" << (void*)reference->GetIntValue() << endl;
              }
            }
            else {
              wcout << L"print: type=System.Base, value=" << (void*)reference->GetIntValue() << endl;
            }
          }
          else {
            wcout << L"print: type=System.Base[], value=" << (void*)reference->GetIntValue();
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size="
										<< reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case FUNC_PARM: {
          StackClass* klass = cur_program->GetClass((long)reference->GetIntValue());
          if(klass) {
            wcout << L"print: type=Functon, class=" << klass->GetName() 
									<< L", method=" << PrintMethod(klass->GetMethod(reference->GetIntValue2())) << endl;
          }
				}
					break;
        }
      }
      else {
        wcout << L"program is not running." << endl;
        is_error = true;
      }
      break;

    case NIL_LIT_EXPR:
      wcout << L"print: type=Nil, value=Nil" << endl;
      break;

    case CHAR_LIT_EXPR:
      wcout << L"print: type=Char, value=" << (char)expression->GetIntValue() << endl;
      break;

    case INT_LIT_EXPR:
      wcout << L"print: type=Int, value=" << expression->GetIntValue() << endl;
      break;

    case FLOAT_LIT_EXPR:
      wcout << L"print: type=Float, value=" << expression->GetFloatValue() << endl;
      break;

    case BOOLEAN_LIT_EXPR:
      wcout << L"print: type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << endl;
      break;

    case AND_EXPR:
    case OR_EXPR:
    case EQL_EXPR:
    case NEQL_EXPR:
    case LES_EXPR:
    case GTR_EQL_EXPR:
    case LES_EQL_EXPR:
    case GTR_EXPR:
      wcout << L"print: type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << endl;
      break;

    case ADD_EXPR:
    case SUB_EXPR:
    case MUL_EXPR:
    case DIV_EXPR:
    case MOD_EXPR:
      if(expression->GetFloatEval()) {
        wcout << L"print: type=Float, value=" << expression->GetFloatValue() << endl;
      }
      else {
        wcout << L"print: type=Int, value=" << expression->GetIntValue() << endl;
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
      Reference* reference = static_cast<Reference*>(expression);
      EvaluateReference(reference, LOCL);
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
      wcout << L"modulus operation requires integer values." << endl;
      is_error = true;
    }
    break;

  default:
    break;
  }
}

void Runtime::Debugger::EvaluateReference(Reference* &reference, MemoryContext context) {
  StackMethod* method = cur_frame->method;
  //
  // instance reference
  //
  if(context != LOCL) {
    if(ref_mem && ref_klass) {
      // check reference name
      bool found;
      StackDclr dclr_value;
      if(context == INST) {
        found = ref_klass->GetInstanceDeclaration(reference->GetVariableName(), dclr_value);
      }
      else {
        found = ref_klass->GetClassDeclaration(reference->GetVariableName(), dclr_value);	
      }

      // set reference
      if(found) {
        reference->SetDeclaration(dclr_value);
        switch(dclr_value.type) {
        case CHAR_PARM:
        case INT_PARM:
          reference->SetIntValue(ref_mem[dclr_value.id]);
          break;

        case FUNC_PARM:
          reference->SetIntValue((long)ref_mem[dclr_value.id]);
          reference->SetIntValue2((long)ref_mem[dclr_value.id + 1]);
          break;

        case FLOAT_PARM: {
          FLOAT_VALUE value;
          memcpy(&value, &ref_mem[dclr_value.id], sizeof(FLOAT_VALUE));
          reference->SetFloatValue(value);
				}
					break;

        case OBJ_PARM:
          EvaluateInstanceReference(reference, dclr_value.id);
          break;

        case BYTE_ARY_PARM:
          EvaluateByteReference(reference, dclr_value.id);
          break;

        case CHAR_ARY_PARM:
          EvaluateCharReference(reference, dclr_value.id);
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
        wcout << L"unknown variable (or no debug information available)." << endl;
        is_error = true;
      }
    }
    else {
      wcout << L"unable to find reference." << endl;
      is_error = true;
    }
  }
  //
  // method reference
  //
  else {
    ref_mem = cur_frame->mem;
    if(ref_mem) {
      StackDclr dclr_value;

      // process explicit '@self' reference
      if(reference->IsSelf()) {
        dclr_value.name = L"@self";
        dclr_value.type = OBJ_PARM;
        reference->SetDeclaration(dclr_value);
        EvaluateInstanceReference(reference, 0);
      }
      // process method reference
      else {
        // check reference name
        bool found = method->GetLocalDeclaration(reference->GetVariableName(), dclr_value);
        reference->SetDeclaration(dclr_value);
        if(found) {
          if(method->HasAndOr()) {
            dclr_value.id++;
          }

          switch(dclr_value.type) {
          case CHAR_PARM:
          case INT_PARM:
            reference->SetIntValue(ref_mem[dclr_value.id + 1]);
            break;

          case FUNC_PARM:
            reference->SetIntValue((long)ref_mem[dclr_value.id + 1]);
            reference->SetIntValue2((long)ref_mem[dclr_value.id + 2]);
            break;

          case FLOAT_PARM: {
            FLOAT_VALUE value;
            memcpy(&value, &ref_mem[dclr_value.id + 1], sizeof(FLOAT_VALUE));
            reference->SetFloatValue(value);
					}
						break;

          case OBJ_PARM:
            EvaluateInstanceReference(reference, dclr_value.id + 1);
            break;

          case BYTE_ARY_PARM:
            EvaluateByteReference(reference, dclr_value.id + 1);
            break;

          case CHAR_ARY_PARM:
            EvaluateCharReference(reference, dclr_value.id + 1);
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
          // class for class reference
          StackClass* klass = cur_program->GetClass(reference->GetVariableName());
          if(klass) {
            dclr_value.name = klass->GetName();
            dclr_value.type = OBJ_PARM;
            reference->SetDeclaration(dclr_value);
            EvaluateClassReference(reference, klass, 0);
          }
          else {
            // process implicit '@self' reference
            Reference* next_reference = TreeFactory::Instance()->MakeReference();
            next_reference->SetReference(reference);
            reference = next_reference;
            // set declaration
            dclr_value.name = L"@self";
            dclr_value.type = OBJ_PARM;
            reference->SetDeclaration(dclr_value);
            EvaluateInstanceReference(reference, 0);
          }
        }
      }
    }
    else {
      wcout << L"unable to de-reference empty frame." << endl;
      is_error = true;
    }
  }
}

void Runtime::Debugger::EvaluateInstanceReference(Reference* reference, int index) {
  if(ref_mem) {
    reference->SetIntValue(ref_mem[index]);
    ref_mem =(size_t*)ref_mem[index];
    ref_klass = MemoryManager::GetClass(ref_mem);
    if(reference->GetReference()) {
      Reference* next_reference = reference->GetReference();
      EvaluateReference(next_reference, INST);
    }
  }
  else {
    wcout << L"current object reference is Nil" << endl;
    is_error = true;
  }
}

void Runtime::Debugger::EvaluateClassReference(Reference* reference, StackClass* klass, int index) {
  size_t* cls_mem = klass->GetClassMemory();
  reference->SetIntValue((size_t)cls_mem);
  ref_mem  =cls_mem;
  ref_klass = klass;
  if(reference->GetReference()) {
    Reference* next_reference = reference->GetReference();
    EvaluateReference(next_reference, CLS);
  }
}

void Runtime::Debugger::EvaluateByteReference(Reference* reference, int index) {
  size_t* array = (size_t*)ref_mem[index];
  if(array) {
    const long max = (long)array[0];
    const long dim = (long)array[1];

    // de-reference array value
    ExpressionList* indices = reference->GetIndices();
    if(indices) {
      // calculate indices values
      vector<Expression*> expressions = indices->GetExpressions();
      vector<long> values;
      for(size_t i = 0; i < expressions.size(); i++) {
        EvaluateExpression(expressions[i]);
        if(expressions[i]->GetExpressionType() == INT_LIT_EXPR) {
          values.push_back(static_cast<IntegerLiteral*>(expressions[i])->GetValue());
        }
        else {
          values.push_back((long)expressions[i]->GetIntValue());
        }
      }
      // match the dimensions
      if((long)expressions.size() == dim) {
        // calculate indices
        array += 2;
        long j = dim - 1;
        long array_index = values[j--];
        for(long i = 1; i < dim; ++i) {
          array_index *= (long)array[i];
          array_index += values[j--];
        }
        array += dim;

        if(array_index > -1 && array_index < max) {
          reference->SetIntValue(((char*)array)[array_index]);
        }
        else {
          wcout << L"array index out of bounds." << endl;
          is_error = true;
        }
      }
      else {
        wcout << L"array dimension mismatch." << endl;
        is_error = true;
      }
    }
    // set array address
    else {
      reference->SetArrayDimension((long)dim);
      reference->SetArraySize((long)max);
      reference->SetIntValue(ref_mem[index]);
    }
  }
  else {
    wcout << L"current array value is Nil" << endl;
    is_error = true;
  }
}

void Runtime::Debugger::EvaluateCharReference(Reference* reference, int index) {
  size_t* array = (size_t*)ref_mem[index];
  if(array) {
    const long max = (long)array[0];
    const long dim = (long)array[1];

    // de-reference array value
    ExpressionList* indices = reference->GetIndices();
    if(indices) {
      // calculate indices values
      vector<Expression*> expressions = indices->GetExpressions();
      vector<int> values;
      for(size_t i = 0; i < expressions.size(); i++) {
        EvaluateExpression(expressions[i]);
        if(expressions[i]->GetExpressionType() == INT_LIT_EXPR) {
          values.push_back(static_cast<IntegerLiteral*>(expressions[i])->GetValue());
        }
        else {
          values.push_back((long)expressions[i]->GetIntValue());
        }
      }
      // match the dimensions
      if(expressions.size() == (size_t)dim) {
        // calculate indices
        array += 2;
        int j = dim - 1;
        long array_index = values[j--];
        for(long i = 1; i < dim; i++) {
          array_index *= (long)array[i];
          array_index += values[j--];
        }
        array += dim;

        if(array_index > -1 && array_index < max) {
          reference->SetIntValue(((wchar_t*)array)[array_index]);
        }
        else {
          wcout << L"array index out of bounds." << endl;
          is_error = true;
        }
      }
      else {
        wcout << L"array dimension mismatch." << endl;
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
  else {
    wcout << L"current array value is Nil" << endl;
    is_error = true;
  }
}

void Runtime::Debugger::EvaluateIntFloatReference(Reference* reference, int index, bool is_float) {
  size_t* array = (size_t*)ref_mem[index];
  if(array) {
    const long max = (long)array[0];
    const int dim = (long)array[1];

    // de-reference array value
    ExpressionList* indices = reference->GetIndices();
    if(indices) {
      // calculate indices values
      vector<Expression*> expressions = indices->GetExpressions();
      vector<int> values;
      for(size_t i = 0; i < expressions.size(); i++) {
        EvaluateExpression(expressions[i]);
        if(expressions[i]->GetExpressionType() == INT_LIT_EXPR) {
          values.push_back(static_cast<IntegerLiteral*>(expressions[i])->GetValue());
        }
        else {
          values.push_back((long)expressions[i]->GetIntValue());
        }
      }
      // match the dimensions
      if(expressions.size() == (size_t)dim) {
        // calculate indices
        array += 2;
        int j = dim - 1;
        long array_index = values[j--];
        for(long i = 1; i < dim; i++) {
          array_index *= (long)array[i];
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
            wcout << L"array index out of bounds." << endl;
            is_error = true;
          }
        }
        // check int array bounds
        else {
          if(array_index > -1 && array_index < max) {
            reference->SetIntValue(array[array_index]);
          }
          else {
            wcout << L"array index out of bounds." << endl;
            is_error = true;
          }
        }
      }
      else {
        wcout << L"array dimension mismatch." << endl;
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
  else {
    wcout << L"current array value is Nil" << endl;
    is_error = true;
  }
}

Command* Runtime::Debugger::ProcessCommand(const wstring &line) {
#ifdef _DEBUG
  wcout << L"input: |" << line << L"|" << endl;
#endif

  // parser input
  is_next = is_next_line = false;
  Parser parser;
  Command* command = parser.Parse(L"?" + line);
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
      wcout << L"\nGoodbye..." << endl;
      exit(0);
      break;

    case LIST_COMMAND: {
      FilePostion* file_pos = static_cast<FilePostion*>(command);

      wstring file_name;
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

      const wstring &path = base_path + file_name;
      if(FileExists(path) && line_num > 0) {
        SourceFile src_file(path, cur_line_num);
        if(!src_file.Print(line_num)) {
          wcout << L"invalid line number." << endl;
          is_error = true;
        }
      }
      else {
        wcout << L"source file or line number doesn't exist, ensure the program is running." << endl;
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
      if(!cur_program) {
        ProcessRun();
      }
      else {
        wcout << L"instance already running." << endl;
        is_error = true;
      }
      break;

    case CLEAR_COMMAND: {
      wcout << L"  are sure you want to clear all breakpoints? [y/n] ";
      wstring line;
      getline(wcin, line);
      if(line == L"y" || line == L"yes") {
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
        wcout << L"program is not running." << endl;
      }
      break;

    case NEXT_LINE_COMMAND:
      if(interpreter) {
        is_next_line = true;
      }
      else {
        wcout << L"program is not running." << endl;
      }
      break;

    case JUMP_OUT_COMMAND:
      if(interpreter) {
        is_jmp_out = true;
      }
      else {
        wcout << L"program is not running." << endl;
      }
      break;

    case CONT_COMMAND:
      if(!interpreter) {
        wcout << L"program is not running." << endl;
      }
      cur_line_num = -2;
      break;

    case INFO_COMMAND:
      ProcessInfo(static_cast<Info*>(command));
      break;

    case STACK_COMMAND:
      if(interpreter) {
        wcout << L"stack:" << endl;
        StackMethod* method = cur_frame->method;
        wcerr << L"  frame: pos=" << cur_call_stack_pos << L", class=" << method->GetClass()->GetName() 
							<< L", method=" << PrintMethod(method);
        const long ip = cur_frame->ip;
        if(ip > -1) {
          StackInstr* instr = cur_frame->method->GetInstruction(ip);
          wcerr << L", file=" << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << endl;
        }
        else {
          wcerr << endl;
        }

        long pos = cur_call_stack_pos - 1;
        do {
          StackMethod* method = cur_call_stack[pos]->method;
          wcerr << L"  frame: pos=" << pos << L", class=" << method->GetClass()->GetName() 
								<< L", method=" << PrintMethod(method);
          const long ip = cur_call_stack[pos]->ip;
          if(ip > -1) {
            StackInstr* instr = cur_call_stack[pos]->method->GetInstruction(ip);
            wcerr << L", file=" << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << endl;
          }
          else {
            wcerr << endl;
          }
        }
        while(--pos);
      }
      else {
        wcout << L"program is not running." << endl;
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
    wcout << L"-- Unable to process command --" << endl;
  }

  is_error = false;
  ref_mem = NULL;
  return NULL;
}

void Runtime::Debugger::ProcessInfo(Info* info) {
  const wstring &cls_name = info->GetClassName();
  const wstring &mthd_name = info->GetMethodName();

#ifdef _DEBUG
  wcout << L"--- info class=" << cls_name << L", method=" << mthd_name << L" ---" << endl;
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
            wcout << L"  class: type=" << klass->GetName() << L", method="
									<< PrintMethod(method) << endl;
						if(method->GetNumberDeclarations() > 0) {
							wcout << L"  parameters:" << endl;
							PrintDeclarations(method->GetDeclarations(), method->GetNumberDeclarations());
						}
          }
        }
        else {
          wcout << L"unable to find method." << endl;
          is_error = true;
        }
      }
      else {
        wcout << L"unable to find class." << endl;
        is_error = true;
      }
    }
    // class info
    else if(cls_name.size() > 0) {
      StackClass* klass = cur_program->GetClass(cls_name);
      if(klass && klass->IsDebug()) {
        wcout << L"  class: type=" << klass->GetName() << endl;
        // print
        wcout << L"  parameters:" << endl;
				if(klass->GetNumberInstanceDeclarations() > 0) {
					PrintDeclarations(klass->GetInstanceDeclarations(), klass->GetNumberInstanceDeclarations());
				}
      }
      else {
        wcout << L"unable to find class." << endl;
        is_error = true;
      }
    }
    // general info
    else {
      wcout << L"general info:" << endl;
      wcout << L"  program executable: file='" << program_file << L"'" << endl;

      // parse method and class names
      const wstring &long_name = cur_frame->method->GetName();
      size_t end_index = long_name.find_last_of(':');
      const wstring &cls_mthd_name = long_name.substr(0, end_index);

      size_t mid_index = cls_mthd_name.find_last_of(':');
      const wstring &cls_name = cls_mthd_name.substr(0, mid_index);
      const wstring &mthd_name = cls_mthd_name.substr(mid_index + 1);

      // print
      wcout << L"  current file='" << cur_file_name << L":" << cur_line_num << L"', method='"
						<< cls_name << L"->" << mthd_name << L"(..)'" << endl;
    }
  }
  else {
    wcout << L"program is not running." << endl;
  }
}

void Runtime::Debugger::ClearBreaks() {
  wcout << L"breakpoints cleared." << endl;
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

  MemoryManager::Clear();
  StackMethod::ClearVirtualEntries();

  cur_line_num = -2;
  cur_frame = NULL;
  cur_program = NULL;
  is_error = false;
  ref_mem = NULL;
  ref_klass = NULL;
  is_jmp_out = false;
}

void Runtime::Debugger::Debug() {
  wcout << L"-------------------------------------" << endl;
  wcout << L"Objeck " << VERSION_STRING << L" - Interactive Debugger" << endl;
  wcout << L"-------------------------------------" << endl << endl;

  if(FileExists(program_file, true) && DirectoryExists(base_path)) {
    wcout << L"loaded executable: file='" << program_file << L"'" << endl;
    wcout << L"source files: path='" << base_path << L"'" << endl << endl;
    // clear arguments
    arguments.clear();
    arguments.push_back(L"obr");
    arguments.push_back(program_file);
  }
  else {
    wcerr << L"unable to load executable or locate base path." << endl;
    exit(1);
  }

  // enter feedback loop
  ClearProgram();
  wcout << L"> ";
  while(true) {    
    wstring line;
    getline(wcin, line);
    if(line.size() > 0) {
      ProcessCommand(line);
      wcout << L"> ";
    }
  }
}

/********************************
 * Debugger main
 ********************************/
int main(int argc, char** argv)
{
  wstring usage;
  // usage += L"Copyright (c) 2010-2014, Randy Hollines. All rights reserved.\n";
  // usage += L"THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
  // usage += L"license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
  // usage += L"FOR MORE INFORMATION.\n\n";
  // usage += L"\n\n";
  usage += L"Usage: obd -exe <program> [-src <source directory>]\n\n";
  usage += L"Parameters:\n";
  usage += L"  -exe: executable file\n";
  usage += L"  -src: source directory path\n\n";
  usage += L"example: \"obd -exe ..\\examples\\hello.obe -src ..\\examples\"\n\nVersion: ";
  usage += VERSION_STRING;
  
  if(argc >= 3) {
#ifdef _WIN32
    // enable Unicode console support
    _setmode(_fileno(stdin), _O_U8TEXT);
    _setmode(_fileno(stdout), _O_U8TEXT);

    WSADATA data;
    int version = MAKEWORD(2, 2);
    if(WSAStartup(version, &data)) {
      cerr << L"Unable to load Winsock 2.2!" << endl;
      exit(1);
    }
#else
    // enable UTF-8 enviroment
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "UTF-8");
#endif

    // reconstruct path
    string buffer;
    for(int i = 1; i < argc; i++) {
      buffer += " ";
      buffer += argv[i];
    }
    const wstring path_string(buffer.begin(), buffer.end());
    map<const wstring, wstring> arguments = ParseCommnadLine(path_string);

    // start debugger
    map<const wstring, wstring>::iterator result = arguments.find(L"exe");
    if(result == arguments.end()) {
      wcerr << usage << endl << endl;
      return 1;
    }
    const wstring &file_name = arguments[L"exe"];

    wstring base_path = L".";
    result = arguments.find(L"src");
    if(result != arguments.end()) {
      base_path = arguments[L"src"];
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
    wcerr << usage << endl << endl;
    return 1;
  }

  return 1;
}
