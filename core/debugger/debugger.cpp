/**************************************************************************
 * Runtime debugger
 *
 * Copyright (c) 2010-2021 Randy Hollines
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
  * Debugger main
  ********************************/
int main(int argc, char** argv)
{
  wstring usage;
  usage += L"Usage: obd -exe <program> [-src <source directory>] [-args \"'<arg 0>' '<arg 1>'\"]\n\n";
  usage += L"Parameters:\n";
  usage += L"  -bin: [input] binary executable file\n";
  usage += L"  -src_dir: [option] source directory path, default is '.'\n";
  usage += L"  -args: [option][end-flag] list of arguments\n\n";
  usage += L"Example: \"obd -exe ..\\examples\\hello.obe -src ..\\examples\"\n\nVersion: ";
  usage += VERSION_STRING;

#if defined(_WIN64) && defined(_WIN32)
  usage += L" (x86-64 Windows)";
#elif _WIN32
  usage += L" (x86 Windows)";
#elif _OSX
#ifdef _ARM64
  usage += L" (macOS ARM64)";
#else
  usage += L" (macOS x86_64)";
#endif
#elif _X64
  usage += L" (x86-64 Linux)";
#elif _ARM32
  usage += L" (ARMv7 Linux)";
#else
  usage += L" (x86 Linux)";
#endif 

  usage += L"\nWeb: www.objeck.org";

  if(argc >= 3) {
#ifdef _WIN32
    // enable Unicode console support
    if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
      return 1;
    }

    if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
      return 1;
    }

    WSADATA data;
    if(WSAStartup(MAKEWORD(2, 2), &data)) {
      cerr << L"Unable to load Winsock 2.2!" << endl;
      exit(1);
    }
#else
    // enable UTF-8 environment
//    setlocale(LC_ALL, "");
 //   setlocale(LC_CTYPE, "UTF-8");
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
    map<const wstring, wstring>::iterator result = arguments.find(L"bin");
    if(result == arguments.end()) {
      wcerr << usage << endl;
      return 1;
    }
    const wstring& file_name_param = arguments[L"bin"];

    wstring base_path_param = L".";
    result = arguments.find(L"src_dir");
    if(result != arguments.end()) {
      base_path_param = arguments[L"src_dir"];
    }

    wstring args_param;
    const wstring args_str = L"args";
    result = arguments.find(args_str);
    if(result != arguments.end()) {
      const size_t start = path_string.find(args_str) + args_str.size();
      args_param = path_string.substr(start, path_string.size() - start);
    }

#ifdef _WIN32
    if(base_path_param.size() > 0 && base_path_param[base_path_param.size() - 1] != '\\') {
      base_path_param += '\\';
    }
#else
    if(base_path_param.size() > 0 && base_path_param[base_path_param.size() - 1] != '/') {
      base_path_param += '/';
    }
#endif

    // go debugger
    Runtime::Debugger debugger(file_name_param, base_path_param, args_param);
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
    wcerr << usage << endl;
    return 1;
  }

  return 1;
}

/********************************
 * Interactive command line
 * debugger
 ********************************/
void Runtime::Debugger::ProcessInstruction(StackInstr* instr, long ip, StackFrame** call_stack, long call_stack_pos, StackFrame* frame)
{
  if(frame->method->GetClass()) {
    const int line_num = instr->GetLineNumber();
    const wstring file_name = frame->method->GetClass()->GetFileName();

    if(line_num > -1) {
      // continue to next line
      if(continue_state == 1 && line_num != cur_line_num && cur_frame && frame->method == cur_frame->method) {
        // wcout << L"--- CONTINE_STATE " << is_continue_state << L" --" << endl;
        continue_state++;
      }

      const bool step_out = is_step_out && call_stack_pos > jump_stack_pos;
      /*
      if(step_out) {
        wcout << L"--- STEP_OUT --" << endl;
      }
      */

      const bool step_into = is_step_into && cur_frame && (frame->method != cur_frame->method || line_num != cur_line_num);
      /*
      if(step_into) {
        wcout << L"--- STEP_INTO --" << endl;
      }
      */

      const bool found_next_line = is_next_line && line_num != cur_line_num && cur_frame && frame->method == cur_frame->method;
      /*
      if(found_next_line) {
        wcout << L"--- NEXT_LINE --" << endl;
      }
      */

      const bool found_break = (continue_state == 2 || line_num != cur_line_num || call_stack_pos != cur_call_stack_pos) && FindBreak(line_num, file_name);
      if(found_break) {
        // wcout << L"--- BREAK_POINT --" << endl;
        continue_state = 0;
      }
      

      if(found_break || found_next_line || step_into || step_out) {
        // set current line
        cur_line_num = line_num;
        cur_file_name = file_name;
        cur_frame = frame;
        cur_call_stack = call_stack;
        cur_call_stack_pos = call_stack_pos;

        is_step_into = is_step_out = is_next_line = false;

        // prompt for input
        const wstring& long_name = cur_frame->method->GetName();
        const size_t end_index = long_name.find_last_of(':');
        const wstring& cls_mthd_name = long_name.substr(0, end_index);

        // show break info
        const size_t mid_index = cls_mthd_name.find_last_of(':');
        const wstring& cls_name = cls_mthd_name.substr(0, mid_index);
        const wstring& mthd_name = cls_mthd_name.substr(mid_index + 1);
        wcout << L"break: file='" << file_name << L":" << line_num << L"', method='" << cls_name << L"->" << mthd_name << L"(..)'" << endl;

        // prompt for break command
        Command* command;
        do {
          wstring line;
          ReadLine(line);
          if(line.size() > 0) {
            command = ProcessCommand(line);
          }
          else {
            command = nullptr;
          }
        } while(!command || (command->GetCommandType() != CONT_COMMAND && command->GetCommandType() != STEP_IN_COMMAND &&
                             command->GetCommandType() != NEXT_LINE_COMMAND && command->GetCommandType() != STEP_OUT_COMMAND));
      }
    }
  }
}
  

void Runtime::Debugger::ProcessSrc(Load* load) 
{
  if(interpreter) {
    wcout << L"unable to modify source path while program is running." << endl;
    return;
  }

  if(FileExists(program_file_param, true) && DirectoryExists(load->GetFileName())) {
    ClearReload();

    base_path_param = load->GetFileName();
#ifdef _WIN32
    if(base_path_param.size() > 0 && base_path_param[base_path_param.size() - 1] != '\\') {
      base_path_param += '\\';
    }
#else
    if(base_path_param.size() > 0 && base_path_param[base_path_param.size() - 1] != '/') {
      base_path_param += '/';
    }
#endif
    wcout << L"source file path: '" << base_path_param << L"'" << endl << endl;
  }
  else {
    wcout << L"unable to locate base path." << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessArgs(Load* load)
{
  ProcessArgs(load->GetFileName());
}

void Runtime::Debugger::ProcessArgs(const wstring& temp)
{
  // clear
  arguments.clear();
  arguments.push_back(L"obr");
  arguments.push_back(program_file_param);
  // parse arguments
  const size_t buffer_max = temp.size() + 1;
  wchar_t* buffer = (wchar_t*)calloc(sizeof(wchar_t), buffer_max);
#ifdef _WIN32
  wcsncpy_s(buffer, buffer_max, temp.c_str(), temp.size());
#else
  wcsncpy(buffer, temp.c_str(), temp.size());
#endif

  wchar_t *state;
#ifdef _WIN32
  wchar_t* token = wcstok_s(buffer, L"'", &state);
#else
  wchar_t* token = wcstok(buffer, L"'", &state);
#endif
  while(token) {
    wstring str_token = Trim(token);
    if(!str_token.empty()) {
      arguments.push_back(str_token);
    }
#ifdef _WIN32
    token = wcstok_s(nullptr, L"'", &state);
#else
    token = wcstok(nullptr, L"'", &state);
#endif
  }
  wcout << L"program arguments sets." << endl;

  // clean up
  free(buffer);
  buffer = nullptr;
}

void Runtime::Debugger::ProcessBin(Load* load) {
  if(interpreter) {
    wcout << L"unable to load executable while program is running." << endl;
    return;
  }

  wstring file_param = load->GetFileName();
  if(!EndsWith(file_param, L".obe")) {
    file_param += L".obe";
  }

  if(FileExists(file_param, true) && DirectoryExists(base_path_param)) {
    // clear program
    ClearReload();
    ClearBreaks();
    program_file_param = file_param;
    // reset arguments
    arguments.clear();
    arguments.push_back(L"obr");
    arguments.push_back(program_file_param);
    wcout << L"loaded binary: '" << program_file_param << L"'" << endl;
  }
  else {
    wcerr << L"unable to load executable='" << program_file_param << "' at locate base path='" << base_path_param << L"'" << endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessRun() {
  if(loader && program_file_param.size() > 0) {
    DoLoad();
    cur_program = loader->GetProgram();
    
    // execute
    op_stack = new size_t[CALC_STACK_SIZE];
    stack_pos = new long;
    (*stack_pos) = 0;

#ifdef _TIMING
    long start = clock();
#endif
    interpreter = new Runtime::StackInterpreter(cur_program, this);
    interpreter->Execute(op_stack, stack_pos, 0, cur_program->GetInitializationMethod(), nullptr, false);
#ifdef _TIMING
    wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << endl;
    wcout << L"---------------------------" << endl;
    wcout << L"Time: " << (float)(clock() - start) / CLOCKS_PER_SEC
          << L" second(s)." << endl;
#endif

#ifdef _DEBUG
    wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << endl;
#endif

    ClearReload();
  }
  else {
    wcout << L"program file not specified." << endl;
  }
}

void Runtime::Debugger::DoLoad()
{
  if(loader) {
    delete loader;
    loader = nullptr;
  }

  // process program parameters
  long argc = (long)arguments.size();
  wchar_t** argv = new wchar_t* [argc];
  for(long i = 0; i < argc; ++i) {
#ifdef _WIN32
    argv[i] = _wcsdup(arguments[i].c_str());
#else
    argv[i] = wcsdup(arguments[i].c_str());
#endif
  }

  // invoke loader
  loader = new Loader(argc, argv);
  loader->Load();

  // clear old program
  for(int i = 0; i < argc; i++) {
    wchar_t* param = argv[i];
    free(param);
    param = nullptr;
  }
  delete[] argv;
  argv = nullptr;
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

  const wstring &path = base_path_param + file_name;
  if(file_name.size() != 0 && FileExists(path)) {
    if(AddBreak(line_num, file_name)) {
      wcout << L"added breakpoint: file='" << file_name << L":" << line_num << L"'" << endl;
    }
    else {
      wcout << L"breakpoint already exist or is invalid" << endl;
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

  const wstring &path = base_path_param + file_name;
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
            const int32_t value = (int32_t)reference->GetIntValue();
            wcout << L"print: type=Int/Byte/Bool, value=" << value << L"/" << (void*)value << endl;
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
            wcout << L"print: type=Byte, value=" << (void*)((unsigned char)reference->GetIntValue()) << endl;
          }
          else {
            wcout << L"print: type=Byte[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case CHAR_ARY_PARM:
          if(reference->GetIndices()) {
            wcout << L"print: type=Char, value=" << (wchar_t)reference->GetIntValue() << endl;
          }
          else {
            wcout << L"print: type=Char[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case INT_ARY_PARM:
          if(reference->GetIndices()) {
            int32_t value = (int32_t)reference->GetIntValue();
            wcout << L"print: type=Int, value=" << value << L"/" << (void*)value << endl;
          }
          else {
            wcout << L"print: type=Int[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case FLOAT_ARY_PARM:
          if(reference->GetIndices()) {
            wcout << L"print: type=Float, value=" << reference->GetFloatValue() << endl;
          }
          else {
            wcout << L"print: type=Float[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
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
              wcout << L"print: type=" << ref_klass->GetName() << L", value=\"" << char_string << L"\"" << endl;
            }
            else {
              wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.IntHolder") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              wcout << L"print: type=System.IntHolder, value=" << (int32_t)instance[0] << endl;
            }
            else {
              wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.ByteHolder") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              wcout << L"print: type=System.ByteHolder, value=" << (void*)((unsigned char)instance[0]) << endl;
            }
            else {
              wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.CharHolder") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              wcout << L"print: type=System.CharHolder, value=" << (wchar_t)instance[0] << endl;
            }
            else {
              wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.FloatHolder") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              FLOAT_VALUE value = *((FLOAT_VALUE*)(&instance[0]));
              wcout << L"print: type=System.FloatHolder, value=" << value << endl;
            }
            else {
              wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
            }
          }
          else {
            wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
          }
          break;

        case OBJ_ARY_PARM:
          if(reference->GetIndices()) {
            StackClass* klass = MemoryManager::GetClass((size_t*)reference->GetIntValue());
            if(klass) {        
              size_t* instance = (size_t*)reference->GetIntValue();
              if(instance) {
                if(klass->GetName() == L"System.String") {
                  size_t* value_instance = (size_t*)instance[0];
                  const wchar_t* char_string = (wchar_t*)(value_instance + 3);
                  wcout << L"print: type=" << klass->GetName() << L", value=\"" << char_string << L"\"" << endl;
                }
                else if(klass->GetName() == L"System.IntHolder") {
                  wcout << L"print: type=System.IntHolder, value=" << (int32_t)instance[0] << endl;
                }
                else if(klass->GetName() == L"System.ByteHolder") {
                  wcout << L"print: type=System.ByteHolder, value=" << (unsigned char)instance[0] << endl;
                }
                else if(klass->GetName() == L"System.CharHolder") {
                  wcout << L"print: type=System.CharHolder, value=" << (wchar_t)instance[0] << endl;
                }
                else if(klass->GetName() == L"System.FloatHolder") {
                  if(instance) {
                    FLOAT_VALUE value = *((FLOAT_VALUE*)(&instance[0]));
                    wcout << L"print: type=System.FloatHolder, value=" << value << endl;
                  }
                  else {
                    wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << endl;
                  }
                }
                else {
                  wcout << L"print: type=" << klass->GetName() << L", value=" << (void*)reference->GetIntValue() << endl;
                }
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
              wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            wcout << endl;
          }
          break;

        case FUNC_PARM: {
          const size_t mthd_cls_id = reference->GetIntValue();
          if(mthd_cls_id > 0) {
            const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
            const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
            StackClass* klass = cur_program->GetClass(cls_id);
            if(klass) {
              wcout << L"print: type=Function, class='" << klass->GetName() << L"', method='" << PrintMethod(klass->GetMethod(mthd_id)) << L"'" << endl;
            }
          }
          else {
            wcout << L"print: type=Function, class=<unknown>, method=<unknown>" << endl;
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
      wcout << L"print: type=Char, value=" << (wchar_t)expression->GetIntValue() << endl;
      break;

    case INT_LIT_EXPR:
      wcout << L"print: type=Int, value=" << (long)expression->GetIntValue() << endl;
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
        wcout << L"print: type=Int, value=" << (long)expression->GetIntValue() << endl;
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
          reference->SetIntValue2(ref_mem[dclr_value.id + 1]);
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
            reference->SetIntValue2(ref_mem[dclr_value.id + 2]);
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
  ref_mem = cls_mem;
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

wstring Runtime::Debugger::PrintMethod(StackMethod* method)
{
  return MethodFormatter::Format(method->GetName());
}

bool Runtime::Debugger::FileExists(const wstring&file_name, bool is_exe /*= false*/)
{
  const string name = UnicodeToBytes(file_name);
  const string ending = ".obl";
  if(ending.size() > name.size() && !equal(ending.rbegin(), ending.rend(), name.rbegin())) {
    return false;
  }

  ifstream touch(name.c_str(), ios::binary);
  if(touch.is_open()) {
    touch.close();
    return true;
  }

  return false;
}

bool Runtime::Debugger::DirectoryExists(const wstring& wdir_name)
{
  const string dir_name = UnicodeToBytes(wdir_name);
#ifdef _WIN32
  HANDLE file = CreateFile(dir_name.c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);

  if(file == INVALID_HANDLE_VALUE) {
    return false;
  }
  CloseHandle(file);

  return true;
#else
  DIR* dir = opendir(dir_name.c_str());
  if(dir) {
    closedir(dir);
    return true;
  }

  return false;
#endif
}

bool Runtime::Debugger::DeleteBreak(int line_num, const wstring& file_name)
{
  UserBreak* user_break = FindBreak(line_num, file_name);
  if(user_break) {
    breaks.remove(user_break);
    return true;
  }

  return false;
}

Runtime::UserBreak* Runtime::Debugger::FindBreak(int line_num)
{
  return FindBreak(line_num, cur_file_name);
}

Runtime::UserBreak* Runtime::Debugger::FindBreak(int line_num, const wstring& file_name)
{
  for(list<UserBreak*>::iterator iter = breaks.begin(); iter != breaks.end(); iter++) {
    UserBreak* user_break = (*iter);
    if(user_break->line_num == line_num && user_break->file_name == file_name) {
      return *iter;
    }
  }

  return nullptr;
}

bool Runtime::Debugger::AddBreak(int line_num, const wstring& file_name)
{
  if(line_num > 0 && !FindBreak(line_num, file_name)) {
    UserBreak* user_break = new UserBreak;
    user_break->line_num = line_num;
    user_break->file_name = file_name;
    breaks.push_back(user_break);
    return true;
  }

  return false;
}

void Runtime::Debugger::ListBreaks()
{
  wcout << L"breaks:" << endl;
  list<UserBreak*>::iterator iter;
  for(iter = breaks.begin(); iter != breaks.end(); iter++) {
    wcout << L"  break: file='" << (*iter)->file_name << L":" << (*iter)->line_num << L"'" << endl;
  }
}

void Runtime::Debugger::PrintDeclarations(StackDclr** dclrs, int dclrs_num)
{
  for(int i = 0; i < dclrs_num; i++) {
    StackDclr* dclr = dclrs[i];

    // parse name
    size_t param_name_index = dclrs[i]->name.find_last_of(':');
    const wstring& param_name = dclrs[i]->name.substr(param_name_index + 1);
    wcout << L"    parameter: name='" << param_name << L"', ";

    // parse type
    switch(dclr->type) {
    case INT_PARM:
      wcout << L"type=Int" << endl;
      break;

    case CHAR_PARM:
      wcout << L"type=Char" << endl;
      break;

    case FLOAT_PARM:
      wcout << L"type=Float" << endl;
      break;

    case BYTE_ARY_PARM:
      wcout << L"type=Byte[]" << endl;
      break;

    case CHAR_ARY_PARM:
      wcout << L"type=Char[]" << endl;
      break;

    case INT_ARY_PARM:
      wcout << L"type=Int[]" << endl;
      break;

    case FLOAT_ARY_PARM:
      wcout << L"type=Float[]" << endl;
      break;

    case OBJ_PARM:
      wcout << L"type=Object" << endl;
      break;

    case OBJ_ARY_PARM:
      wcout << L"type=Object[]" << endl;
      break;

    case FUNC_PARM:
      wcout << L"type=Function" << endl;
      break;
    }
  }
}

Command* Runtime::Debugger::ProcessCommand(const wstring &line) {
#ifdef _DEBUG
  wcout << L"input: |" << line << L"|" << endl;
#endif

  // parser input
  Parser parser;
  Command* command = parser.Parse(L"?" + line);
  if(command) {
    switch(command->GetCommandType()) {
    case EXE_COMMAND:
      ProcessBin(static_cast<Load*>(command));
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

      const wstring &path = base_path_param + file_name;
      if(FileExists(path) && line_num > 0) {
        SourceFile src_file(path, cur_line_num, this);
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
      wcout << L"  are sure you want to clear all breakpoints? [y/n]" << endl << L"  ";
      wstring line;
      ReadLine(line);
      if(line == L"y" || line == L"yes") {
        ClearBreaks();
      }
    }
      break;

    case DELETE_COMMAND:
      ProcessDelete(static_cast<FilePostion*>(command));
      break;

    case STEP_IN_COMMAND:
      if(interpreter) {
        is_step_into = true;
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

    case STEP_OUT_COMMAND:
      if(interpreter) {
        is_step_out = true;
        jump_stack_pos = cur_call_stack_pos;
      }
      else {
        wcout << L"program is not running." << endl;
      }
      break;

    case CONT_COMMAND:
      if(interpreter) {
        continue_state = 1;
      }
      else {
        wcout << L"program is not running." << endl;
      }
      break;

    case MEMORY_COMMAND:
      if(interpreter) {
        const size_t alloc_mem = MemoryManager::GetAllocationSize();
        const size_t max_mem = MemoryManager::GetMaxMemory();
        wcout << L"memory: allocated=" << ToFloat(alloc_mem) << L", collection=" << ToFloat(max_mem) << endl;
      }
      else {
        wcout << L"program is not running." << endl;
      }
      break;

    case INFO_COMMAND:
      ProcessInfo(static_cast<Info*>(command));
      break;

    case STACK_COMMAND:
      if(interpreter) {
        wcout << L"stack:" << endl;
        StackMethod* method = cur_frame->method;
        wcerr << L"  frame: pos=" << cur_call_stack_pos << L", class='" << method->GetClass()->GetName() << L"', method='" << PrintMethod(method) << L"'";
        const long ip = cur_frame->ip;
        if(ip > -1) {
          StackInstr* instr = cur_frame->method->GetInstruction(ip);
          wcerr << L", file=" << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << endl;
        }
        else {
          wcerr << endl;
        }

        long pos = cur_call_stack_pos;
        while(pos--) {
          StackMethod* method = cur_call_stack[pos]->method;
          if(method->GetClass()) {
            wcerr << L"  frame: pos=" << pos << L", class='" << method->GetClass()->GetName() << L"', method='" << PrintMethod(method) << "'";
            const long ip = cur_call_stack[pos]->ip;
            if(ip > -1) {
              StackInstr* instr = cur_call_stack[pos]->method->GetInstruction(ip);
              wcerr << L", file=" << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << endl;
            }
            else {
              wcerr << endl;
            }
          }
        }
        

      }
      else {
        wcout << L"program is not running." << endl;
      }
      break;

    default:
      break;
    }

    is_error = false;
    ref_mem = nullptr;
    return command;
  }
  else {
    wcout << L"-- Unable to process command --" << endl;
  }

  is_error = false;
  ref_mem = nullptr;
  return nullptr;
}

void Runtime::Debugger::ProcessInfo(Info* info) {
  const wstring &cls_name = info->GetClassName();
  const wstring &mthd_name = info->GetMethodName();

#ifdef _DEBUG
  wcout << L"--- info class='" << cls_name << L"', method='" << mthd_name << L"' ---" << endl;
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
          PrintDeclarations(klass->GetClassDeclarations(), klass->GetNumberClassDeclarations());
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
      wcout << L"  program executable: file='" << program_file_param << L"'" << endl;

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
    tmp = nullptr;
  }
}

void Runtime::Debugger::ClearProgram() {
  if(loader) {
    delete loader;
    loader = nullptr;
  }
  
  if(interpreter) {
    delete interpreter;
    interpreter = nullptr;
  }

  if(op_stack) {
    delete[] op_stack;
    op_stack = nullptr;
  }

  if(stack_pos) {
    delete stack_pos;
    stack_pos = nullptr;
  }

  MemoryManager::Clear();
  StackMethod::ClearVirtualEntries();

  is_step_into = is_next_line = is_step_out = false;
  continue_state = 0;
  cur_line_num = -1;
  cur_frame = nullptr;
  cur_program = nullptr;
  is_error = false;
  ref_mem = nullptr;
  ref_klass = nullptr;
  is_step_out = false;
}

void Runtime::Debugger::Debug() {
  wcout << L"-------------------------------------" << endl;
  wcout << L"Objeck " << VERSION_STRING << L" - Interactive Debugger" << endl;
  wcout << L"-------------------------------------" << endl << endl;

  if(!EndsWith(program_file_param, L".obe")) {
    program_file_param += L".obe";
  }

  if(FileExists(program_file_param, true) && DirectoryExists(base_path_param)) {
    wcout << L"loaded binary: '" << program_file_param << L"'" << endl;
    wcout << L"source file path: '" << base_path_param << L"'" << endl << endl;
    // clear arguments
    arguments.clear();
    arguments.push_back(L"obr");
    arguments.push_back(program_file_param);
  }
  else {
    wcerr << L"unable to load executable='" << program_file_param << "' at locate base path='" << base_path_param << L"'" << endl;
    exit(1);
  }

  // find starting file
  ClearReload();
  StackMethod* start = loader->GetStartMethod();
  if(start) {
    cur_file_name = start->GetClass()->GetFileName();
  }

  if(!args_param.empty()) {
    ProcessArgs(args_param);
  }

  // enter feedback loop
  while(true) {
    wstring line;
    ReadLine(line);
    if(line.size() > 0) {
      ProcessCommand(line);
    }
  }
}

bool Runtime::SourceFile::Print(int start)
{
  const int window = 5;
  int end = start + window * 2;
  start--;

  if(start >= end || start >= (int)lines.size()) {
    return false;
  }

  if(start - window > 0) {
    start -= window;
    end -= window;
  }
  else {
    start = 0;
    end = window * 2;
  }

  // find leading whitespace
  int leading = 160;
  for(int i = start; i < (int)lines.size() && i < (int)end; i++) {
    const wstring line = lines[i];
    int j = 0;
    while(j < (int)line.size() && (line[j] == L' ' || line[j] == L'\t')) {
      j++;
    }

    if(j != 0 && leading > j) {
      leading = j;
    }
  }

  for(int i = start; i < (int)lines.size() && i < (int)end; i++) {
    // trim leading whitespace
    wstring line = lines[i];
    const int line_size = (int)line.size();
    if(line_size > 0 && (line[0] == L' ' || line[0] == L'\t') && line_size > leading) {
      line = line.substr(leading);
    }

    const bool is_cur_line_num = i + 1 == cur_line_num;
    const bool is_break_point = debugger->FindBreak(i + 1);
    if(is_cur_line_num && is_break_point) {
      wcout << right << L"#>" << setw(window) << (i + 1) << L": " << line << endl;
    }
    else if(is_cur_line_num) {
      wcout << right << L"=>" << setw(window) << (i + 1) << L": " << line << endl;
    }
    else if(is_break_point) {
      wcout << right << L"# " << setw(window) << (i + 1) << L": " << line << endl;
    }
    else {
      wcout << right << setw(window + 2) << (i + 1) << L": " << line << endl;
    }
  }

  return true;
}
