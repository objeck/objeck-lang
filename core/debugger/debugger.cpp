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
#include "dap.h"
#include "../shared/sys.h"
#include "../shared/version.h"
#include <sstream>

 /********************************
  * Debugger main
  ********************************/
int main(int argc, const char* argv[])
{
  std::wstring usage;
  usage += L"Usage: obd [options]\n\n";
  usage += L"Options:\n";
  usage += L"  --binary, -bin, -b <file>       Objeck binary file (required)\n";
  usage += L"  --source-dir, -src_dir <path>   Source directory path (default: '.')\n";
  usage += L"  --arguments, -args, -a '<args>' Program arguments\n";
  usage += L"\nExamples:\n";
  usage += L"  obd --binary hello.obe\n";
  usage += L"  obd -b hello.obe --source-dir ../examples\n";
  usage += L"  obd -bin hello.obe -src_dir . -a \"'arg1' 'arg2'\"\n";
  usage += L"\nVersion: ";
  usage += VERSION_STRING;

#if defined(_WIN64) && defined(_WIN32) && defined(_M_ARM64)
  usage += L" (arm64 Windows)";
#elif defined(_WIN64) && defined(_WIN32)
  usage += L" (x86-64 Windows)";
#elif _WIN32
  usage += L" (x86 Windows)";
#elif _OSX
#ifdef _ARM64
  usage += L" (macOS ARM64)";
#else
  usage += L" (macOS x86_64)";
#endif
#elif _ARM64
  usage += L" (Linux ARM64)";
#elif _X64
  usage += L" (x86-64 Linux)";
#elif _ARM32
  usage += L" (ARMv7 Linux)";
#else
  usage += L" (x86 Linux)";
#endif 

  usage += L"\nWeb: https://www.objeck.org";

  // Check for DAP mode
  bool dap_mode = false;
  for(int i = 1; i < argc; i++) {
    if(std::string(argv[i]) == "--dap") {
      dap_mode = true;
      break;
    }
  }

  if(dap_mode) {
#ifdef _WIN32
    WSADATA data;
    if(WSAStartup(MAKEWORD(2, 2), &data)) {
      return 1;
    }
#endif
    Runtime::DapAdapter adapter;
    adapter.Run();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
  }

  if(argc >= 3) {
#ifdef _WIN32
    WSADATA data;
    if(WSAStartup(MAKEWORD(2, 2), &data)) {
      std::wcerr << L"Unable to load Winsock 2.2!" << std::endl;
      exit(1);
    }

    // set to efficiency mode
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE PowerThrottling = { 0 };
    PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    PowerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));
#endif

    // Parse command line using enhanced parser
    CommandLineParseResult cmd_result = ParseCommandLine(argc, argv);
    std::map<const std::wstring, std::wstring> arguments = cmd_result.arguments;

    // Check for binary file (support --binary, -bin, -b)
    std::wstring file_name_param = GetCommandLineArgumentWithAliases(
      arguments,
      {L"binary", L"bin", L"b"}
    );

    if(file_name_param.empty()) {
      std::wcerr << usage << std::endl;
      return 1;
    }

    // Check for source directory (support --source-dir, -src_dir, -src)
    std::wstring base_path_param = GetCommandLineArgumentWithAliases(
      arguments,
      {L"source-dir", L"src-dir", L"src_dir", L"src"},
      L"."
    );

    // Check for program arguments (support --arguments, -args, -a)
    std::wstring args_param = GetCommandLineArgumentWithAliases(
      arguments,
      {L"arguments", L"args", L"a"}
    );

    // If args found, extract remaining command line after the args flag
    if(!args_param.empty()) {
      // Find position of args in reconstructed path
      const std::wstring& cmd_line = cmd_result.reconstructed_path;
      size_t args_pos = cmd_line.find(L"-args");
      if(args_pos == std::wstring::npos) {
        args_pos = cmd_line.find(L"--arguments");
      }
      if(args_pos != std::wstring::npos) {
        // Find the value start
        size_t value_start = cmd_line.find_first_not_of(L" \t", args_pos + 5);
        if(value_start != std::wstring::npos) {
          args_param = cmd_line.substr(value_start);
        }
      }
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
    std::wcerr << usage << std::endl;
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
    const std::wstring file_name = frame->method->GetClass()->GetFileName();

    if(line_num > -1) {
      // continue to next line
      if(continue_state == 1 && line_num != cur_line_num && cur_method && frame->method == cur_method) {
        // std::wcout << L"--- CONTINE_STATE " << is_continue_state << L" --" << std::endl;
        continue_state++;
      }

      const bool step_out = is_step_out && call_stack_pos < jump_stack_pos;
      /*
      if(step_out) {
        std::wcout << L"--- STEP_OUT --" << std::endl;
      }
      */

      const bool step_into = is_step_into && cur_method && (frame->method != cur_method || line_num != cur_line_num);
      /*
      if(step_into) {
        std::wcout << L"--- STEP_INTO --" << std::endl;
      }
      */

      // Step-over: stop at next line in same method, or at caller if method returned
      const bool found_next_line = is_next_line && cur_method &&
        ((line_num != cur_line_num && frame->method == cur_method) ||
         (frame->method != cur_method && call_stack_pos < cur_call_stack_pos));
      /*
      if(found_next_line) {
        std::wcout << L"--- NEXT_LINE --" << std::endl;
      }
      */

      // only consider a breakpoint on the first opcode of each line-entry, so a
      // multi-opcode source line counts as a single hit (matters for ignore/temp)
      const bool new_line_instr = (line_num != prev_instr_line || call_stack_pos != prev_instr_pos);
      prev_instr_line = line_num;
      prev_instr_pos = call_stack_pos;

      UserBreak* hit_break = ((new_line_instr && (continue_state == 2 || line_num != cur_line_num || call_stack_pos != cur_call_stack_pos)) ? FindBreak(line_num, file_name) : nullptr);
      bool found_break = (hit_break != nullptr);

      // honor disabled breakpoints
      if(found_break && !hit_break->enabled) {
        found_break = false;
      }

      // evaluate conditional breakpoint
      if(found_break && hit_break->condition) {
        // set frame context for expression evaluation
        cur_frame = eval_frame = frame;
        cur_call_stack = call_stack;
        cur_call_stack_pos = call_stack_pos;
        ref_mem = nullptr;
        ref_klass = nullptr;
        is_error = false;

        EvaluateExpression(hit_break->condition);

        if(!is_error) {
          if(hit_break->condition->GetFloatEval()) {
            found_break = (hit_break->condition->GetFloatValue() != 0.0);
          }
          else {
            found_break = (hit_break->condition->GetIntValue() != 0);
          }
        }
        else {
          found_break = false;
        }
        is_error = false;
      }

      // honor ignore counts (skip the next N hits); new_line_instr guarantees this
      // runs once per logical hit, not once per opcode on the line
      if(found_break && hit_break->ignore_count > 0) {
        hit_break->ignore_count--;
        found_break = false;
      }

      // DAP logpoints: log the message (with {expr} interpolation) and resume
      // instead of stopping
      if(found_break && !hit_break->log_message.empty() && mode == DebugMode::DAP && dap_adapter) {
        cur_frame = eval_frame = frame;
        cur_call_stack = call_stack;
        cur_call_stack_pos = call_stack_pos;
        eval_frame_pos = call_stack_pos;
        dap_adapter->EmitLogPoint(hit_break->log_message);
        found_break = false;
      }

      // one-shot (temporary) breakpoints fire once then vanish
      const bool temp_hit = (found_break && hit_break->temporary);

      // run-to-line (until)
      bool until_hit = false;
      if(until_line > 0 && line_num == until_line &&
         (until_file.empty() || file_name == until_file || FindBreak(line_num, file_name))) {
        until_hit = (line_num != cur_line_num || call_stack_pos != cur_call_stack_pos);
      }
      if(until_hit) {
        until_line = -1;
        until_file.clear();
      }

      // data breakpoints (watchpoints)
      const bool watch_hit = CheckWatches(frame, call_stack, call_stack_pos);

      if(found_break || until_hit || watch_hit) {
        continue_state = 0;
      }

      if(temp_hit) {
        breaks.remove(hit_break);
        delete hit_break;
        hit_break = nullptr;
      }

      if(found_break || found_next_line || step_into || step_out || until_hit || watch_hit) {
        // set current line
        cur_line_num = line_num;
        cur_file_name = file_name;
        cur_frame = frame;
        cur_method = frame->method;
        cur_call_stack = call_stack;
        cur_call_stack_pos = call_stack_pos;
        // reset the inspection frame to the top of the stack on every stop
        eval_frame = frame;
        eval_frame_pos = call_stack_pos;

        is_step_into = is_step_out = is_next_line = false;

        if(mode == DebugMode::DAP && dap_adapter) {
          // DAP mode: notify adapter and block until resume
          std::string reason = found_break ? "breakpoint" : "step";
          dap_adapter->OnStopped(reason, line_num, file_name, frame, call_stack, call_stack_pos);

          // After resume, check what stepping mode was requested
          if(dap_adapter->IsDisconnected() || dap_adapter->IsRestarting()) {
            // cleanly unwind the parked run (disconnect ends the session;
            // restart loops the worker back into a fresh DapRun)
            if(interpreter) {
              interpreter->RequestHalt();
            }
            return;
          }
          if(dap_adapter->IsStepInto()) {
            is_step_into = true;
          }
          else if(dap_adapter->IsStepOver()) {
            is_next_line = true;
          }
          else if(dap_adapter->IsStepOut()) {
            is_step_out = true;
            jump_stack_pos = call_stack_pos;
          }
          else {
            // continue — set state for next breakpoint
            continue_state = 1;
          }
        }
        else {
          // CLI mode: interactive prompt
          // prompt for input
          const std::wstring& long_name = cur_frame->method->GetName();
          const size_t end_index = long_name.find_last_of(':');
          const std::wstring& cls_mthd_name = long_name.substr(0, end_index);

          // show break info
          const size_t mid_index = cls_mthd_name.find_last_of(':');
          const std::wstring& cls_name = cls_mthd_name.substr(0, mid_index);
          const std::wstring& mthd_name = cls_mthd_name.substr(mid_index + 1);
          std::wcout << L"break: file='" << C(CLR_CYAN) << file_name << L":" << line_num << C(CLR_RESET) << L"', method='" << C(CLR_GREEN) << cls_name << L"->" << mthd_name << L"(..)" << C(CLR_RESET) << L"'" << std::endl;

          // prompt for break command
          Command* command;
          do {
            std::wstring line;
            ReadLine(line);
            // gdb-style: empty input repeats the previous command
            if(line.empty()) {
              line = last_cmd;
            }
            if(line.size() > 0) {
              command = ProcessCommand(line);
            }
            else {
              command = nullptr;
            }
          } while(!command || (command->GetCommandType() != CONT_COMMAND && command->GetCommandType() != STEP_IN_COMMAND &&
                               command->GetCommandType() != NEXT_LINE_COMMAND && command->GetCommandType() != STEP_OUT_COMMAND &&
                               command->GetCommandType() != UNTIL_COMMAND));
        }
      }
    }
  }
}

void Runtime::Debugger::OnRuntimeError(StackFrame** call_stack, long call_stack_pos, StackFrame* frame)
{
  // Only break for DAP clients that enabled the "uncaught" exception filter.
  if(mode != DebugMode::DAP || !dap_adapter || !dap_adapter->BreaksOnException()) {
    return;
  }
  if(!frame || !frame->method) {
    return;
  }

  int line_num = -1;
  std::wstring file_name;
  if(frame->method->GetClass()) {
    file_name = frame->method->GetClass()->GetFileName();
  }
  if(frame->ip > -1) {
    line_num = frame->method->GetInstruction(frame->ip)->GetLineNumber();
  }

  // the faulting instruction may carry no line; fall back to the nearest caller
  // frame that does, so the editor lands on a meaningful source location
  if(line_num <= 0 && call_stack) {
    for(long pos = call_stack_pos - 1; pos >= 0; --pos) {
      StackFrame* caller = call_stack[pos];
      if(caller && caller->method && caller->ip > -1) {
        const int ln = caller->method->GetInstruction(caller->ip)->GetLineNumber();
        if(ln > 0) {
          line_num = ln;
          if(caller->method->GetClass()) {
            file_name = caller->method->GetClass()->GetFileName();
          }
          frame = caller;
          break;
        }
      }
    }
  }

  // publish the error site as the current stop context
  cur_line_num = line_num;
  cur_file_name = file_name;
  cur_frame = frame;
  cur_method = frame->method;
  cur_call_stack = call_stack;
  cur_call_stack_pos = call_stack_pos;
  eval_frame = frame;
  eval_frame_pos = call_stack_pos;

  dap_adapter->OnStopped("exception", line_num, file_name, frame, call_stack, call_stack_pos);
}


void Runtime::Debugger::ProcessSrc(Load* load) 
{
  if(interpreter) {
    std::wcout << L"unable to modify source path while program is running." << std::endl;
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
    std::wcout << L"source file path: '" << base_path_param << L"'" << std::endl << std::endl;
  }
  else {
    std::wcout << L"unable to locate base path." << std::endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessArgs(Load* load)
{
  ProcessArgs(load->GetFileName());
}

void Runtime::Debugger::ProcessArgs(const std::wstring& temp)
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
    std::wstring str_token = Trim(token);
    if(!str_token.empty()) {
      arguments.push_back(str_token);
    }
#ifdef _WIN32
    token = wcstok_s(nullptr, L"'", &state);
#else
    token = wcstok(nullptr, L"'", &state);
#endif
  }
  std::wcout << L"program arguments sets." << std::endl;

  // clean up
  free(buffer);
  buffer = nullptr;
}

void Runtime::Debugger::ProcessBin(Load* load) {
  if(interpreter) {
    std::wcout << L"unable to load executable while program is running." << std::endl;
    return;
  }

  std::wstring file_param = load->GetFileName();
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
    std::wcout << L"loaded binary: '" << program_file_param << L"'" << std::endl;
  }
  else {
    std::wcerr << L"unable to load executable='" << program_file_param << "' at locate base path='" << base_path_param << L"'" << std::endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessRun() {
  if(loader && program_file_param.size() > 0) {
    DoLoad();
    cur_program = loader->GetProgram();
    // resolve any function breakpoints queued before load
    ResolvePendingMethodBreaks();

    // execute
    op_stack = new size_t[CALC_STACK_SIZE];
    stack_pos = new size_t;
    (*stack_pos) = 0;

#ifdef _TIMING
    long start = clock();
#endif
    interpreter = new Runtime::StackInterpreter(cur_program, this);
    interpreter->Execute(op_stack, stack_pos, 0, cur_program->GetInitializationMethod(), nullptr, false);
#ifdef _TIMING
    std::wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << std::endl;
    std::wcout << L"---------------------------" << std::endl;
    std::wcout << L"Time: " << (float)(clock() - start) / CLOCKS_PER_SEC
          << L" second(s)." << std::endl;
#endif

#ifdef _DEBUG
    std::wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << std::endl;
#endif

    ClearReload();
  }
  else {
    std::wcout << L"program file not specified." << std::endl;
  }
}

void Runtime::Debugger::DoLoad()
{
  bool do_mem_init = false;
  if(loader) {
    delete loader;
    loader = nullptr;
  }
  else {
    do_mem_init = true;
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

  // Initialize on first load, or re-initialize after a prior run tore the
  // MemoryManager down (a DAP restart re-runs DapRun in-process, and the
  // previous run's cleanup called MemoryManager::Clear()).
  if(do_mem_init || !MemoryManager::IsInitialized()) {
    MemoryManager::Initialize(loader->GetProgram(), 0);
    cur_line_num = 1;
  }

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
  const bool temporary = (break_command->GetCommandType() == TBREAK_COMMAND);
  Expression* condition = break_command->GetCondition();

  int line_num = break_command->GetLineNumber();
  std::wstring file_name = break_command->GetFileName();

  // method form: b Class->Method  (resolve to the method's first executable line)
  const std::wstring& mthd_name = break_command->GetMethodName();
  if(!mthd_name.empty()) {
    std::wstring resolved_file;
    int resolved_line = -1;
    if(MethodEntryLocation(file_name, mthd_name, resolved_file, resolved_line)) {
      file_name = resolved_file;
      line_num = resolved_line;
    }
    else {
      std::wcout << L"unable to resolve method '" << file_name << L"->" << mthd_name
                 << L"' (ensure the binary is loaded and compiled with debug symbols)." << std::endl;
      is_error = true;
      return;
    }
  }

  if(line_num < 0) {
    line_num = cur_line_num;
  }
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }

  std::wstring path;
  if(FileExists(file_name)) {
    path = file_name;
  }
  else {
    path = base_path_param + file_name;
  }

  if(file_name.size() != 0 && FileExists(path)) {
    // warn/relocate when the requested line carries no executable instruction
    if(mthd_name.empty() && !LineHasInstruction(file_name, line_num)) {
      const int near_line = NearestExecutableLine(file_name, line_num);
      if(near_line > 0 && near_line != line_num) {
        std::wcout << C(CLR_YELLOW) << L"note: line " << line_num
                   << L" has no executable code; moved breakpoint to line " << near_line << C(CLR_RESET) << std::endl;
        line_num = near_line;
      }
      else if(near_line <= 0) {
        std::wcout << C(CLR_YELLOW) << L"warning: line " << line_num
                   << L" has no executable code; this breakpoint may never trigger." << C(CLR_RESET) << std::endl;
      }
    }

    if(AddBreak(line_num, file_name, condition, temporary)) {
      std::wcout << L"added breakpoint: file='" << C(CLR_CYAN) << file_name << L":" << line_num << C(CLR_RESET) << L"'";
      if(condition) {
        std::wcout << L" " << C(CLR_YELLOW) << L"[conditional]" << C(CLR_RESET);
      }
      if(temporary) {
        std::wcout << L" " << C(CLR_YELLOW) << L"[temporary]" << C(CLR_RESET);
      }
      std::wcout << std::endl;
    }
    else {
      std::wcout << L"breakpoint already exist or is invalid" << std::endl;
    }
  }
  else {
    std::wcout << L"file doesn't exist or isn't loaded." << std::endl;
    is_error = true;
  }
}

void Runtime::Debugger::ProcessBreaks() {
  if(breaks.size() > 0) {
    ListBreaks();
  }
  else {
    std::wcout << L"no breakpoints defined." << std::endl;
  }
}

void Runtime::Debugger::ProcessDelete(FilePostion* delete_command) {
  int line_num = delete_command->GetLineNumber();
  if(line_num < 0) {
    line_num = cur_line_num;
  }

  std::wstring file_name = delete_command->GetFileName();
  if(file_name.size() == 0) {
    file_name = cur_file_name;
  }

  std::wstring path;
  if(FileExists(file_name)) {
    path = file_name;
  }
  else {
    path = base_path_param + file_name;
  }

  if(file_name.size() != 0 && FileExists(path)) {
    if(DeleteBreak(line_num, file_name)) {
      std::wcout << L"removed breakpoint: file='" << file_name << L":" << line_num << L"'" << std::endl;
    }
    else {
      std::wcout << L"breakpoint doesn't exist." << std::endl;
    }
  }
  else {
    std::wcout << L"file doesn't exist or isn't loaded." << std::endl;
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
            std::wcout << L"cannot reference scalar variable" << std::endl;
          }
          else {
            std::wcout << L"print: " << C(CLR_BLUE) << L"type=Char" << C(CLR_RESET) << L", " << C(CLR_BOLD) << L"value=" << (wchar_t)reference->GetIntValue() << C(CLR_RESET) << std::endl;
          }
          break;

        case INT_PARM:
          if(reference->GetIndices()) {
            std::wcout << L"cannot reference scalar variable" << std::endl;
          }
          else {
            const long value = (long)reference->GetIntValue();
            std::ios_base::fmtflags flags(std::wcout.flags());
            std::wcout << L"print: " << C(CLR_BLUE) << L"type=Int/Byte/Bool" << C(CLR_RESET) << L", " << C(CLR_BOLD) << L"value=" << value << L"(0x" << std::hex << value << L')' << C(CLR_RESET) << std::endl;
            std::wcout.flags(flags);
          }
          break;


        case FLOAT_PARM:
          if(reference->GetIndices()) {
            std::wcout << L"cannot reference scalar variable" << std::endl;
          }
          else {
            std::wcout << L"print: " << C(CLR_BLUE) << L"type=Float" << C(CLR_RESET) << L", " << C(CLR_BOLD) << L"value=" << reference->GetFloatValue() << C(CLR_RESET) << std::endl;
          }
          break;

        case BYTE_ARY_PARM:
          if(reference->GetIndices()) {
            std::wcout << L"print: type=Byte, value=" << (void*)((unsigned char)reference->GetIntValue()) << std::endl;
          }
          else {
            std::wcout << L"print: type=Byte[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              std::wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            std::wcout << std::endl;
          }
          break;

        case CHAR_ARY_PARM:
          if(reference->GetIndices()) {
            std::wcout << L"print: type=Char, value=" << (wchar_t)reference->GetIntValue() << L"(" << (void*)expression->GetIntValue() << L")";
          }
          else {
            std::wcout << L"print: type=Char[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              std::wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            std::wcout << std::endl;
          }
          break;

        case INT_ARY_PARM:
          if(reference->GetIndices()) {
            const long value = (long)reference->GetIntValue();
            std::ios_base::fmtflags flags(std::wcout.flags());
            std::wcout << L"print: type=Int, value=" << value << L"(0x" << std::hex << value << L')' << std::endl;
            std::wcout.flags(flags);
          }
          else {
            std::wcout << L"print: type=Int[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              std::wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            std::wcout << std::endl;
          }
          break;

        case FLOAT_ARY_PARM:
          if(reference->GetIndices()) {
            std::wcout << L"print: " << C(CLR_BLUE) << L"type=Float" << C(CLR_RESET) << L", " << C(CLR_BOLD) << L"value=" << reference->GetFloatValue() << C(CLR_RESET) << std::endl;
          }
          else {
            std::wcout << L"print: type=Float[], value=" << reference->GetIntValue() << L"(" << (void*)reference->GetIntValue() << L")";
            if(reference->GetArrayDimension()) {
              std::wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            std::wcout << std::endl;
          }
          break;

        case OBJ_PARM:
          if(ref_klass && ref_klass->GetName() == L"System.String") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              size_t* string_instance = (size_t*)instance[0];
              const wchar_t* char_string = (wchar_t*)(string_instance + 3);
              std::wcout << L"print: type=" << ref_klass->GetName() << L", value=\"" << char_string << L"\"" << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          //
          // start: Generic collections
          //
          else if(ref_klass && (ref_klass->GetName() == L"Collection.Vector" || ref_klass->GetName() == L"Collection.CompareVector")) {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance && !reference->GetIndices()) {
              size_t* vector_instance = (size_t*)instance[0];
              const long vector_size = (long)vector_instance[1];
              std::wcout << L"print: type=" << ref_klass->GetName() << L", size=" << vector_size << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && (ref_klass->GetName() == L"Collection.List" || ref_klass->GetName() == L"Collection.CompareList")) {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance && !reference->GetIndices()) {
              size_t* list_instance = (size_t*)instance[0];
              const long list_size = (long)list_instance[1];
              std::wcout << L"print: type=" << ref_klass->GetName() << L", size=" << list_size << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"Collection.Hash") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance && !reference->GetIndices()) {
              size_t* hash_instance = (size_t*)instance[0];
              const long hash_size = (long)hash_instance[1];
              const long hash_capacity = (long)hash_instance[2];
              std::wcout << L"print: type=" << ref_klass->GetName() << L", size=" << hash_size << L", capacity=" << hash_capacity << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"Collection.Map") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance && !reference->GetIndices()) {
              size_t* map_instance = (size_t*)instance[0];
              const long map_size = (long)map_instance[2];
              std::wcout << L"print: type=" << ref_klass->GetName() << L", size=" << map_size << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          //
          // end: Generic collections
          //
          else if(ref_klass && ref_klass->GetName() == L"System.IntRef") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              std::wcout << L"print: type=System.IntHolder, value=" << (long)instance[0] << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.BoolRef") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              std::wcout << L"print: type=System.BoolHolder, value=" << (instance[0] ? L"false" : L"true") << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.ByteRef") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              std::wcout << L"print: type=System.ByteHolder, value=" << (void*)((unsigned char)instance[0]) << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.CharRef") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              std::wcout << L"print: type=System.CharHolder, value=" << (wchar_t)instance[0] << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else if(ref_klass && ref_klass->GetName() == L"System.FloatRef") {
            size_t* instance = (size_t*)reference->GetIntValue();
            if(instance) {
              FLOAT_VALUE value = *((FLOAT_VALUE*)(&instance[0]));
              std::wcout << L"print: type=System.FloatHolder, value=" << value << std::endl;
            }
            else {
              std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else {
            std::wcout << L"print: type=" << (ref_klass ? ref_klass->GetName() : L"System.Base") << L", value=" << (void*)reference->GetIntValue() << std::endl;
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
                  std::wcout << L"print: type=" << klass->GetName() << L", value=\"" << char_string << L"\"" << std::endl;
                }
                else if(klass->GetName() == L"System.IntRef") {
                  std::wcout << L"print: type=System.IntHolder, value=" << (long)instance[0] << std::endl;
                }
                else if(klass->GetName() == L"System.BoolRef") {
                  std::wcout << L"print: type=System.BoolHolder, value=" << (instance[0] ? L"false" : L"true") << std::endl;
                }
                else if(klass->GetName() == L"System.ByteRef") {
                  std::wcout << L"print: type=System.ByteHolder, value=" << (unsigned char)instance[0] << std::endl;
                }
                else if(klass->GetName() == L"System.CharRef") {
                  std::wcout << L"print: type=System.CharHolder, value=" << (wchar_t)instance[0] << std::endl;
                }
                else if(klass->GetName() == L"System.FloatRef") {
                  FLOAT_VALUE value = *((FLOAT_VALUE*)(&instance[0]));
                  std::wcout << L"print: type=System.FloatHolder, value=" << value << std::endl;
                }
                else {
                  std::wcout << L"print: type=" << klass->GetName() << L", value=" << (void*)reference->GetIntValue() << std::endl;
                }
              }
              else {
                std::wcout << L"print: type=System.Base, value=" << (void*)reference->GetIntValue() << std::endl;
              }
            }
            else {
              std::wcout << L"print: type=System.Base, value=" << (void*)reference->GetIntValue() << std::endl;
            }
          }
          else {
            std::wcout << L"print: type=System.Base[], value=" << (void*)reference->GetIntValue();
            if(reference->GetArrayDimension()) {
              std::wcout << L", dimension=" << reference->GetArrayDimension() << L", size=" << reference->GetArraySize();
            }
            std::wcout << std::endl;
          }
          break;

        case FUNC_PARM: {
          const size_t mthd_cls_id = reference->GetIntValue();
          if(mthd_cls_id > 0) {
            const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
            const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
            StackClass* klass = cur_program->GetClass(cls_id);
            if(klass) {
              std::wcout << L"print: type=Function, class='" << klass->GetName() << L"', method='" << PrintMethod(klass->GetMethod(mthd_id)) << L"'" << std::endl;
            }
          }
          else {
            std::wcout << L"print: type=Function, class=<unknown>, method=<unknown>" << std::endl;
          }
        }
          break;
        }
      }
      else {
        std::wcout << L"program is not running." << std::endl;
        is_error = true;
      }
      break;

    case NIL_LIT_EXPR:
      std::wcout << L"print: type=Nil, value=Nil" << std::endl;
      break;

    case CHAR_LIT_EXPR:
      std::wcout << L"print: type=Char, value=" << (wchar_t)expression->GetIntValue() << L"(0x" << (void*)expression->GetIntValue() << L")" << std::endl;
      break;

    case INT_LIT_EXPR:
      std::wcout << L"print: type=Int, value=" << (long)expression->GetIntValue() << L"(0x" << (void*)expression->GetIntValue() << L")" << std::endl;
      break;

    case FLOAT_LIT_EXPR:
      std::wcout << L"print: type=Float, value=" << expression->GetFloatValue() << std::endl;
      break;

    case BOOLEAN_LIT_EXPR:
      std::wcout << L"print: type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << std::endl;
      break;

    case AND_EXPR:
    case OR_EXPR:
    case EQL_EXPR:
    case NEQL_EXPR:
    case LES_EXPR:
    case GTR_EQL_EXPR:
    case LES_EQL_EXPR:
    case GTR_EXPR:
      std::wcout << L"print: type=Bool, value=" << (expression->GetIntValue() ? "true" : "false" ) << std::endl;
      break;

    case ADD_EXPR:
    case SUB_EXPR:
    case MUL_EXPR:
    case DIV_EXPR:
    case MOD_EXPR:
      if(expression->GetFloatEval()) {
        std::wcout << L"print: type=Float, value=" << expression->GetFloatValue() << std::endl;
      }
      else {
        std::wcout << L"print: type=Int, value=" << (long)expression->GetIntValue() << L"(0x" << (void*)expression->GetIntValue() << L")" << std::endl;
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
      std::wcout << L"modulus operation requires integer values." << std::endl;
      is_error = true;
    }
    break;

  default:
    break;
  }
}

void Runtime::Debugger::EvaluateReference(Reference* &reference, MemoryContext context) {
  // evaluate against the frame currently selected for inspection (frame/up/down)
  StackFrame* eframe = eval_frame ? eval_frame : cur_frame;
  StackMethod* method = eframe->method;
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
          ref_slot = &ref_mem[dclr_value.id];
          ref_slot_type = dclr_value.type;
          break;

        case FUNC_PARM:
          reference->SetIntValue((long)ref_mem[dclr_value.id]);
          reference->SetIntValue2(ref_mem[dclr_value.id]);
          break;

        case FLOAT_PARM: {
          FLOAT_VALUE value;
          memcpy(&value, &ref_mem[dclr_value.id], sizeof(FLOAT_VALUE));
          reference->SetFloatValue(value);
          ref_slot = &ref_mem[dclr_value.id];
          ref_slot_type = dclr_value.type;
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
        std::wcout << L"unknown variable (or no debug information available)." << std::endl;
        is_error = true;
      }
    }
    else {
      std::wcout << L"unable to find reference." << std::endl;
      is_error = true;
    }
  }
  //
  // method reference
  //
  else {
    ref_mem = eframe->mem;
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
        // check reference name locally
        int offset = 1;
        MemoryContext context = LOCL;
        bool found = method->GetDeclaration(reference->GetVariableName(), dclr_value);
        if(!found) {
          // check reference name at instance and class levels
          found = method->GetClass()->GetDeclaration(reference->GetVariableName(), dclr_value, context);
          offset = 0;
        }
        reference->SetDeclaration(dclr_value);

        if(found) {
          if(context == LOCL && method->HasAndOr()) {
            dclr_value.id++;
          }
          else if(context == INST) {
            ref_mem = (size_t*)eframe->mem[0];
          }
          else if(context == CLS) {
            ref_mem = eframe->method->GetClass()->GetClassMemory();
          }

          switch(dclr_value.type) {
          case CHAR_PARM:
          case INT_PARM:
            reference->SetIntValue(ref_mem[dclr_value.id + offset]);
            // capture slot for 'set'
            ref_slot = &ref_mem[dclr_value.id + offset];
            ref_slot_type = dclr_value.type;
            break;

          case FUNC_PARM:
            reference->SetIntValue((long)ref_mem[dclr_value.id + offset]);
            reference->SetIntValue2(ref_mem[dclr_value.id + 2]);
            break;

          case FLOAT_PARM: {
            FLOAT_VALUE value;
            memcpy(&value, &ref_mem[dclr_value.id + offset], sizeof(FLOAT_VALUE));
            reference->SetFloatValue(value);
            // capture slot for 'set'
            ref_slot = &ref_mem[dclr_value.id + offset];
            ref_slot_type = dclr_value.type;
          }
            break;

          case OBJ_PARM:
            EvaluateInstanceReference(reference, dclr_value.id + offset);
            break;

          case BYTE_ARY_PARM:
            EvaluateByteReference(reference, dclr_value.id + offset);
            break;

          case CHAR_ARY_PARM:
            EvaluateCharReference(reference, dclr_value.id + offset);
            break;

          case INT_ARY_PARM:
            EvaluateIntFloatReference(reference, dclr_value.id + offset, false);
            break;

          case OBJ_ARY_PARM:
            EvaluateIntFloatReference(reference, dclr_value.id + offset, false);
            break;

          case FLOAT_ARY_PARM:
            EvaluateIntFloatReference(reference, dclr_value.id + offset, true);
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
      std::wcout << L"unable to de-reference empty frame." << std::endl;
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
    std::wcout << L"current object reference is Nil" << std::endl;
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
      std::vector<Expression*> expressions = indices->GetExpressions();
      std::vector<long> values;
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
          std::wcout << L"array index out of bounds." << std::endl;
          is_error = true;
        }
      }
      else {
        std::wcout << L"array dimension mismatch." << std::endl;
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
    std::wcout << L"current array value is Nil" << std::endl;
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
      std::vector<Expression*> expressions = indices->GetExpressions();
      std::vector<int> values;
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
          std::wcout << L"array index out of bounds." << std::endl;
          is_error = true;
        }
      }
      else {
        std::wcout << L"array dimension mismatch." << std::endl;
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
    std::wcout << L"current array value is Nil" << std::endl;
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
      std::vector<Expression*> expressions = indices->GetExpressions();
      std::vector<int> values;
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
            std::wcout << L"array index out of bounds." << std::endl;
            is_error = true;
          }
        }
        // check int array bounds
        else {
          if(array_index > -1 && array_index < max) {
            reference->SetIntValue(array[array_index]);
          }
          else {
            std::wcout << L"array index out of bounds." << std::endl;
            is_error = true;
          }
        }
      }
      else {
        std::wcout << L"array dimension mismatch." << std::endl;
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
    std::wcout << L"current array value is Nil" << std::endl;
    is_error = true;
  }
}

std::wstring Runtime::Debugger::PrintMethod(StackMethod* method)
{
  return MethodFormatter::Format(method->GetName());
}

bool Runtime::Debugger::FileExists(const std::wstring&file_name, bool is_exe /*= false*/)
{
  const std::string name = UnicodeToBytes(file_name);
  const std::string ending = ".obl";
  if(ending.size() > name.size() && !equal(ending.rbegin(), ending.rend(), name.rbegin())) {
    return false;
  }

  std::ifstream touch(name.c_str(), std::ios::binary);
  if(touch.is_open()) {
    touch.close();
    return true;
  }

  return false;
}

bool Runtime::Debugger::DirectoryExists(const std::wstring& wdir_name)
{
  const std::string dir_name = UnicodeToBytes(wdir_name);
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

bool Runtime::Debugger::DeleteBreak(int line_num, const std::wstring& file_name)
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

Runtime::UserBreak* Runtime::Debugger::FindBreak(int line_num, const std::wstring& file_name)
{
  // Extract basename from instruction's file_name for fallback comparison
  std::wstring instr_basename = file_name;
  size_t sep = file_name.find_last_of(L"/\\");
  if(sep != std::wstring::npos) {
    instr_basename = file_name.substr(sep + 1);
  }

  for(std::list<UserBreak*>::iterator iter = breaks.begin(); iter != breaks.end(); iter++) {
    UserBreak* user_break = (*iter);
    const std::wstring base_file_name = base_path_param + user_break->file_name;
    if(user_break->line_num == line_num &&
       (user_break->file_name == file_name || base_file_name == file_name || user_break->file_name == instr_basename)) {
      return *iter;
    }
  }

  return nullptr;
}

// forward declarations for file-scope helpers defined further below
static StackProgram* DbgProgram(StackProgram* cur_program, Loader* loader);
static StackFrame* DbgFrameAt(long pos, StackFrame* top, StackFrame** call_stack, long top_pos);

bool Runtime::Debugger::AddBreak(int line_num, const std::wstring& file_name, Expression* condition, bool temporary, const std::wstring& log_message)
{
  if(line_num > 0 && !FindBreak(line_num, file_name)) {
    UserBreak* user_break = new UserBreak;
    user_break->id = next_break_id++;
    user_break->line_num = line_num;
    user_break->file_name = file_name;
    user_break->condition = condition;
    user_break->enabled = true;
    user_break->temporary = temporary;
    user_break->ignore_count = 0;
    user_break->last_line = -1;
    user_break->last_pos = -1;
    user_break->log_message = log_message;
    breaks.push_back(user_break);
    return true;
  }

  return false;
}

std::wstring Runtime::Debugger::SetVariableForDap(int frame_index, const std::wstring& name, const std::wstring& value_str)
{
  if(!interpreter) {
    return L"<error>";
  }

  // select the requested frame for evaluation
  StackFrame* saved_eval = eval_frame;
  const long saved_pos = eval_frame_pos;
  eval_frame = DbgFrameAt(frame_index, cur_frame, cur_call_stack, cur_call_stack_pos);
  eval_frame_pos = frame_index;

  // resolve the target slot
  ref_slot = nullptr;
  ref_slot_type = -1;
  ref_mem = nullptr;
  ref_klass = nullptr;
  is_error = false;
  Reference* reference = TreeFactory::Instance()->MakeReference(name);
  EvaluateExpression(reference);

  std::wstring result = L"<error>";
  if(!is_error && ref_slot) {
    size_t* slot = ref_slot;
    const int slot_type = ref_slot_type;
    try {
      const std::string narrow = UnicodeToBytes(value_str);
      if(slot_type == FLOAT_PARM) {
        FLOAT_VALUE fv = std::stod(narrow);
        memcpy(slot, &fv, sizeof(FLOAT_VALUE));
      }
      else {
        const long iv = std::stol(narrow, nullptr, 0);
        *slot = (size_t)iv;
      }
      result = value_str;
    }
    catch(...) {
      result = L"<error>";
    }
  }

  // restore the inspection frame to the stopped top
  is_error = false;
  eval_frame = saved_eval;
  eval_frame_pos = saved_pos;
  return result;
}

bool Runtime::Debugger::AddMethodBreak(const std::wstring& spec, const std::wstring& condition_str)
{
  // if the symbol table isn't loaded yet (DAP sets these before the program
  // runs), queue the spec and resolve it once the program loads
  if(!DbgProgram(cur_program, loader)) {
    pending_method_breaks.push_back(std::make_pair(spec, condition_str));
    return true;
  }

  std::wstring cls_name;
  std::wstring mthd_name;

  const size_t arrow = spec.find(L"->");
  if(arrow != std::wstring::npos) {
    cls_name = spec.substr(0, arrow);
    mthd_name = spec.substr(arrow + 2);
  }
  else {
    const size_t dot = spec.find_last_of(L'.');
    if(dot != std::wstring::npos) {
      cls_name = spec.substr(0, dot);
      mthd_name = spec.substr(dot + 1);
    }
    else {
      mthd_name = spec;
    }
  }

  std::wstring file;
  int line = -1;
  bool resolved = false;
  if(!cls_name.empty()) {
    resolved = MethodEntryLocation(cls_name, mthd_name, file, line);
  }
  else {
    // bare method name: search all debug classes for a match
    StackProgram* prog = DbgProgram(cur_program, loader);
    if(prog) {
      StackClass** classes = prog->GetClasses();
      const long class_num = prog->GetClassNumber();
      for(long i = 0; i < class_num && !resolved; ++i) {
        if(classes[i]->IsDebug()) {
          resolved = MethodEntryLocation(classes[i]->GetName(), mthd_name, file, line);
        }
      }
    }
  }

  if(!resolved) {
    return false;
  }

  Expression* condition = nullptr;
  if(!condition_str.empty()) {
    condition = ParseCondition(condition_str);
  }
  return AddBreak(line, file, condition);
}

bool Runtime::Debugger::AddLogPoint(int line_num, const std::wstring& file_name, const std::wstring& log_message)
{
  return AddBreak(line_num, file_name, nullptr, false, log_message);
}

void Runtime::Debugger::ResolvePendingMethodBreaks()
{
  if(pending_method_breaks.empty() || !DbgProgram(cur_program, loader)) {
    return;
  }

  std::vector<std::pair<std::wstring, std::wstring>> pending;
  pending.swap(pending_method_breaks);
  for(size_t i = 0; i < pending.size(); ++i) {
    AddMethodBreak(pending[i].first, pending[i].second);
  }
}

void Runtime::Debugger::ListBreaks()
{
  std::wcout << L"breaks:" << std::endl;
  std::list<UserBreak*>::iterator iter;
  for(iter = breaks.begin(); iter != breaks.end(); iter++) {
    UserBreak* user_break = *iter;
    std::wcout << L"  break #" << user_break->id << L": file='" << C(CLR_CYAN) << user_break->file_name << L":" << user_break->line_num << C(CLR_RESET) << L"'";
    if(user_break->condition) {
      std::wcout << L" " << C(CLR_YELLOW) << L"[conditional]" << C(CLR_RESET);
    }
    if(user_break->temporary) {
      std::wcout << L" " << C(CLR_YELLOW) << L"[temporary]" << C(CLR_RESET);
    }
    if(!user_break->enabled) {
      std::wcout << L" " << C(CLR_RED) << L"[disabled]" << C(CLR_RESET);
    }
    if(user_break->ignore_count > 0) {
      std::wcout << L" " << C(CLR_YELLOW) << L"[ignore " << user_break->ignore_count << L"]" << C(CLR_RESET);
    }
    std::wcout << std::endl;
  }
}

// basename helper (strip directory components)
static std::wstring DbgBaseName(const std::wstring& f) {
  const size_t sep = f.find_last_of(L"/\\");
  return (sep == std::wstring::npos) ? f : f.substr(sep + 1);
}

// returns the loaded program (available after the binary is loaded, even pre-run)
static StackProgram* DbgProgram(StackProgram* cur_program, Loader* loader) {
  if(cur_program) {
    return cur_program;
  }
  return loader ? loader->GetProgram() : nullptr;
}

bool Runtime::Debugger::LineHasInstruction(const std::wstring& file_name, int line_num) {
  StackProgram* prog = DbgProgram(cur_program, loader);
  if(!prog) {
    // can't validate without a loaded program; assume valid
    return true;
  }

  const std::wstring want = DbgBaseName(file_name);
  StackClass** classes = prog->GetClasses();
  const long class_num = prog->GetClassNumber();
  for(long i = 0; i < class_num; ++i) {
    StackClass* klass = classes[i];
    if(!klass->IsDebug() || DbgBaseName(klass->GetFileName()) != want) {
      continue;
    }
    StackMethod** methods = klass->GetMethods();
    const int mthd_num = klass->GetMethodCount();
    for(int m = 0; m < mthd_num; ++m) {
      StackMethod* method = methods[m];
      const long instr_num = method->GetInstructionCount();
      for(long k = 0; k < instr_num; ++k) {
        if(method->GetInstruction(k)->GetLineNumber() == line_num) {
          return true;
        }
      }
    }
  }

  return false;
}

int Runtime::Debugger::NearestExecutableLine(const std::wstring& file_name, int line_num) {
  StackProgram* prog = DbgProgram(cur_program, loader);
  if(!prog) {
    return -1;
  }

  const std::wstring want = DbgBaseName(file_name);
  int best = -1;
  int best_dist = INT_MAX;
  StackClass** classes = prog->GetClasses();
  const long class_num = prog->GetClassNumber();
  for(long i = 0; i < class_num; ++i) {
    StackClass* klass = classes[i];
    if(!klass->IsDebug() || DbgBaseName(klass->GetFileName()) != want) {
      continue;
    }
    StackMethod** methods = klass->GetMethods();
    const int mthd_num = klass->GetMethodCount();
    for(int m = 0; m < mthd_num; ++m) {
      StackMethod* method = methods[m];
      const long instr_num = method->GetInstructionCount();
      for(long k = 0; k < instr_num; ++k) {
        const int ln = method->GetInstruction(k)->GetLineNumber();
        // prefer the closest line at or after the requested line
        if(ln >= line_num && (ln - line_num) < best_dist) {
          best_dist = ln - line_num;
          best = ln;
        }
      }
    }
  }

  return best;
}

bool Runtime::Debugger::MethodEntryLocation(const std::wstring& cls_name, const std::wstring& mthd_name, std::wstring& out_file, int& out_line) {
  StackProgram* prog = DbgProgram(cur_program, loader);
  if(!prog) {
    return false;
  }

  StackClass* klass = prog->GetClass(cls_name);
  if(!klass || !klass->IsDebug()) {
    return false;
  }

  std::vector<StackMethod*> methods = klass->GetMethods(mthd_name);
  if(methods.empty()) {
    return false;
  }

  // first method overload; land on the first body line (skip the signature line
  // so parameters are live), falling back to the entry line if there is no body
  StackMethod* method = methods[0];
  const long instr_num = method->GetInstructionCount();
  int entry_line = -1;
  for(long k = 0; k < instr_num; ++k) {
    const int ln = method->GetInstruction(k)->GetLineNumber();
    if(ln <= 0) {
      continue;
    }
    if(entry_line < 0) {
      entry_line = ln;
    }
    else if(ln > entry_line) {
      out_file = DbgBaseName(klass->GetFileName());
      out_line = ln;
      return true;
    }
  }

  if(entry_line > 0) {
    out_file = DbgBaseName(klass->GetFileName());
    out_line = entry_line;
    return true;
  }

  return false;
}

// returns the StackFrame for a logical frame position (top == cur_call_stack_pos)
static StackFrame* DbgFrameAt(long pos, StackFrame* top, StackFrame** call_stack, long top_pos) {
  if(pos >= top_pos) {
    return top;
  }
  if(pos < 0) {
    return call_stack[0];
  }
  return call_stack[pos];
}

void Runtime::Debugger::ShowFrame() {
  if(!interpreter) {
    std::wcout << L"program is not running." << std::endl;
    return;
  }

  StackFrame* frame = DbgFrameAt(eval_frame_pos, cur_frame, cur_call_stack, cur_call_stack_pos);
  StackMethod* method = frame->method;
  std::wcout << L"frame #" << eval_frame_pos << L": class='" << C(CLR_GREEN) << method->GetClass()->GetName()
             << C(CLR_RESET) << L"', method='" << C(CLR_GREEN) << PrintMethod(method) << C(CLR_RESET) << L"'";
  const long ip = frame->ip;
  if(ip > -1) {
    StackInstr* instr = method->GetInstruction(ip);
    std::wcout << L", file=" << C(CLR_CYAN) << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << C(CLR_RESET);
  }
  std::wcout << std::endl;
}

void Runtime::Debugger::ProcessFrame(Frame* frame) {
  if(!interpreter) {
    std::wcout << L"program is not running." << std::endl;
    return;
  }

  const int num = frame->GetFrameNumber();
  if(num >= 0) {
    if(num > cur_call_stack_pos) {
      std::wcout << L"no such frame (valid range 0.." << cur_call_stack_pos << L")." << std::endl;
      return;
    }
    eval_frame_pos = num;
    eval_frame = DbgFrameAt(eval_frame_pos, cur_frame, cur_call_stack, cur_call_stack_pos);
  }
  ShowFrame();
}

void Runtime::Debugger::ProcessFrameShift(int delta) {
  if(!interpreter) {
    std::wcout << L"program is not running." << std::endl;
    return;
  }

  long new_pos = eval_frame_pos + delta;
  if(new_pos < 0) {
    new_pos = 0;
    std::wcout << L"already at outermost frame." << std::endl;
  }
  else if(new_pos > cur_call_stack_pos) {
    new_pos = cur_call_stack_pos;
    std::wcout << L"already at innermost frame." << std::endl;
  }
  eval_frame_pos = new_pos;
  eval_frame = DbgFrameAt(eval_frame_pos, cur_frame, cur_call_stack, cur_call_stack_pos);
  ShowFrame();
}

void Runtime::Debugger::ProcessLocals() {
  if(!interpreter) {
    std::wcout << L"program is not running." << std::endl;
    return;
  }

  StackFrame* frame = DbgFrameAt(eval_frame_pos, cur_frame, cur_call_stack, cur_call_stack_pos);
  StackMethod* method = frame->method;
  StackDclr** dclrs = method->GetDeclarations();
  const long dclr_num = method->GetNumberDeclarations();
  if(dclr_num <= 0) {
    std::wcout << L"no locals in this frame." << std::endl;
    return;
  }

  std::wcout << L"locals (frame #" << eval_frame_pos << L"):" << std::endl;
  for(long i = 0; i < dclr_num; ++i) {
    const std::wstring& full = dclrs[i]->name;
    const std::wstring bare = full.substr(full.find_last_of(L':') + 1);
    if(bare.empty()) {
      continue;
    }
    std::wcout << L"  " << C(CLR_BOLD) << bare << C(CLR_RESET) << L" -> ";
    Reference* reference = TreeFactory::Instance()->MakeReference(bare);
    Print* print = TreeFactory::Instance()->MakePrint(reference);
    ProcessPrint(print);
  }
}

void Runtime::Debugger::ProcessSet(Set* set) {
  if(!interpreter) {
    std::wcout << L"program is not running." << std::endl;
    return;
  }

  // resolve the target slot
  ref_slot = nullptr;
  ref_slot_type = -1;
  ref_mem = nullptr;
  ref_klass = nullptr;
  is_error = false;
  EvaluateExpression(set->GetReference());
  if(is_error || !ref_slot) {
    std::wcout << L"cannot set: target must be an assignable Int/Char/Float variable." << std::endl;
    is_error = false;
    return;
  }

  // capture slot before evaluating the value (which reuses ref_slot)
  size_t* slot = ref_slot;
  const int slot_type = ref_slot_type;

  // evaluate the new value
  ref_mem = nullptr;
  ref_klass = nullptr;
  is_error = false;
  Expression* value = set->GetValue();
  EvaluateExpression(value);
  if(is_error) {
    std::wcout << L"cannot set: invalid value expression." << std::endl;
    is_error = false;
    return;
  }

  if(slot_type == FLOAT_PARM) {
    FLOAT_VALUE fv = value->GetFloatEval() ? value->GetFloatValue() : (FLOAT_VALUE)(long)value->GetIntValue();
    memcpy(slot, &fv, sizeof(FLOAT_VALUE));
    std::wcout << L"set: " << C(CLR_BOLD) << L"value=" << fv << C(CLR_RESET) << std::endl;
  }
  else {
    const size_t iv = value->GetFloatEval() ? (size_t)(long)value->GetFloatValue() : (size_t)value->GetIntValue();
    *slot = iv;
    std::ios_base::fmtflags flags(std::wcout.flags());
    std::wcout << L"set: " << C(CLR_BOLD) << L"value=" << (long)iv << L"(0x" << std::hex << (long)iv << L")" << C(CLR_RESET) << std::endl;
    std::wcout.flags(flags);
  }
}

void Runtime::Debugger::ProcessEnableDisable(int id, bool enable) {
  if(breaks.empty()) {
    std::wcout << L"no breakpoints defined." << std::endl;
    return;
  }

  int count = 0;
  for(std::list<UserBreak*>::iterator iter = breaks.begin(); iter != breaks.end(); ++iter) {
    if(id < 0 || (*iter)->id == id) {
      (*iter)->enabled = enable;
      count++;
    }
  }

  if(count == 0) {
    std::wcout << L"no breakpoint with id #" << id << L"." << std::endl;
  }
  else {
    std::wcout << (enable ? L"enabled " : L"disabled ") << count << L" breakpoint(s)." << std::endl;
  }
}

void Runtime::Debugger::ProcessIgnore(int id, int count) {
  UserBreak* target = nullptr;
  for(std::list<UserBreak*>::iterator iter = breaks.begin(); iter != breaks.end(); ++iter) {
    if((*iter)->id == id) {
      target = *iter;
      break;
    }
  }

  if(!target) {
    std::wcout << L"no breakpoint with id #" << id << L"." << std::endl;
    return;
  }

  target->ignore_count = count;
  std::wcout << L"breakpoint #" << id << L" will be ignored for the next " << count << L" hit(s)." << std::endl;
}

void Runtime::Debugger::ProcessUntil(FilePostion* until_command) {
  if(!interpreter) {
    std::wcout << L"program is not running." << std::endl;
    return;
  }

  int line_num = until_command->GetLineNumber();
  if(line_num <= 0) {
    std::wcout << L"usage: until <line>" << std::endl;
    return;
  }

  std::wstring file_name = until_command->GetFileName();
  if(file_name.empty()) {
    file_name = cur_file_name;
  }

  until_line = line_num;
  until_file = file_name;
  continue_state = 1;
  std::wcout << L"running until " << C(CLR_CYAN) << DbgBaseName(file_name) << L":" << line_num << C(CLR_RESET) << std::endl;
}

void Runtime::Debugger::ProcessWatch(Watch* watch) {
  WatchPoint* wp = new WatchPoint;
  wp->id = next_watch_id++;
  wp->expression = watch->GetExpression();
  wp->text = watch->GetText();
  wp->has_value = false;
  wp->is_float = false;
  wp->last_int = 0;
  wp->last_float = 0.0;
  watches.push_back(wp);
  std::wcout << L"added watchpoint #" << wp->id << L"." << std::endl;
}

void Runtime::Debugger::ProcessUnwatch(int id) {
  for(std::list<WatchPoint*>::iterator iter = watches.begin(); iter != watches.end(); ++iter) {
    if(id < 0 || (*iter)->id == id) {
      WatchPoint* wp = *iter;
      iter = watches.erase(iter);
      delete wp;
      std::wcout << L"removed watchpoint." << std::endl;
      if(id >= 0) {
        return;
      }
      if(iter == watches.end()) {
        break;
      }
    }
  }
}

void Runtime::Debugger::ProcessWatches() {
  if(watches.empty()) {
    std::wcout << L"no watchpoints defined." << std::endl;
    return;
  }

  std::wcout << L"watches:" << std::endl;
  for(std::list<WatchPoint*>::iterator iter = watches.begin(); iter != watches.end(); ++iter) {
    std::wcout << L"  watch #" << (*iter)->id << std::endl;
  }
}

bool Runtime::Debugger::CheckWatches(StackFrame* frame, StackFrame** call_stack, long call_stack_pos) {
  if(watches.empty()) {
    return false;
  }

  // evaluate each watch expression against the current (top) frame
  StackFrame* saved_eval = eval_frame;
  eval_frame = frame;

  bool triggered = false;
  for(std::list<WatchPoint*>::iterator iter = watches.begin(); iter != watches.end(); ++iter) {
    WatchPoint* wp = *iter;
    if(!wp->expression) {
      continue;
    }

    ref_mem = nullptr;
    ref_klass = nullptr;
    is_error = false;
    EvaluateExpression(wp->expression);
    if(is_error) {
      // variable not in scope in this frame; skip
      is_error = false;
      continue;
    }

    const bool is_float = wp->expression->GetFloatEval();
    bool changed = false;
    size_t new_int = 0;
    FLOAT_VALUE new_float = 0.0;
    if(is_float) {
      new_float = wp->expression->GetFloatValue();
      changed = wp->has_value && (new_float != wp->last_float);
    }
    else {
      new_int = (size_t)wp->expression->GetIntValue();
      changed = wp->has_value && (new_int != wp->last_int);
    }

    if(changed && mode == DebugMode::CLI) {
      std::wcout << C(CLR_YELLOW) << L"watch #" << wp->id << L" changed: ";
      if(is_float) {
        std::wcout << L"old=" << wp->last_float << L", new=" << new_float;
      }
      else {
        std::wcout << L"old=" << (long)wp->last_int << L", new=" << (long)new_int;
      }
      std::wcout << C(CLR_RESET) << std::endl;
    }

    wp->is_float = is_float;
    wp->last_int = new_int;
    wp->last_float = new_float;
    wp->has_value = true;
    if(changed) {
      triggered = true;
    }
  }

  is_error = false;
  eval_frame = saved_eval;
  return triggered;
}

void Runtime::Debugger::PrintDeclarations(StackDclr** dclrs, int dclrs_num)
{
  for(int i = 0; i < dclrs_num; i++) {
    StackDclr* dclr = dclrs[i];

    // parse name
    size_t param_name_index = dclrs[i]->name.find_last_of(':');
    const std::wstring& param_name = dclrs[i]->name.substr(param_name_index + 1);
    std::wcout << L"    parameter: name='" << param_name << L"', ";

    // parse type
    switch(dclr->type) {
    case INT_PARM:
      std::wcout << L"type=Int" << std::endl;
      break;

    case CHAR_PARM:
      std::wcout << L"type=Char" << std::endl;
      break;

    case FLOAT_PARM:
      std::wcout << L"type=Float" << std::endl;
      break;

    case BYTE_ARY_PARM:
      std::wcout << L"type=Byte[]" << std::endl;
      break;

    case CHAR_ARY_PARM:
      std::wcout << L"type=Char[]" << std::endl;
      break;

    case INT_ARY_PARM:
      std::wcout << L"type=Int[]" << std::endl;
      break;

    case FLOAT_ARY_PARM:
      std::wcout << L"type=Float[]" << std::endl;
      break;

    case OBJ_PARM:
      std::wcout << L"type=Object" << std::endl;
      break;

    case OBJ_ARY_PARM:
      std::wcout << L"type=Object[]" << std::endl;
      break;

    case FUNC_PARM:
      std::wcout << L"type=Function" << std::endl;
      break;
    }
  }
}

Command* Runtime::Debugger::ProcessCommand(const std::wstring &line) {
#ifdef _DEBUG
  std::wcout << L"input: |" << line << L"|" << std::endl;
#endif

  // remember the command so an empty Enter can repeat it (gdb-style)
  last_cmd = line;

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
      std::wcout << C(CLR_RESET) << L"\nGoodbye..." << std::endl;
      exit(0);
      break;

    case LIST_COMMAND: {
      FilePostion* file_pos = static_cast<FilePostion*>(command);

      std::wstring file_name;
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

      std::wstring path;
      if(FileExists(file_name)) {
        path = file_name;
      }
      else if(FileExists(base_path_param + file_name)) {
        path = base_path_param + file_name;
      }
      else {
        // try basename only (handles relative paths from compiler)
        std::wstring basename = file_name;
        size_t sep = file_name.find_last_of(L"/\\");
        if(sep != std::wstring::npos) {
          basename = file_name.substr(sep + 1);
        }
        path = base_path_param + basename;
      }

      if(line_num > 0 && FileExists(path)) {
        SourceFile src_file(path, cur_line_num, this);
        if(!src_file.Print(line_num)) {
          std::wcout << L"invalid line number." << std::endl;
          is_error = true;
        }
      }
      else {
        std::wcout << L"source file or line number doesn't exist, ensure the program is running." << std::endl;
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
        std::wcout << L"instance already running." << std::endl;
        is_error = true;
      }
      break;

    case CLEAR_COMMAND: {
      std::wcout << L"  are sure you want to clear all breakpoints? [y/n]" << std::endl << L"  ";
      std::wstring line;
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
        std::wcout << L"program is not running." << std::endl;
      }
      break;

    case NEXT_LINE_COMMAND:
      if(interpreter) {
        is_next_line = true;
      }
      else {
        std::wcout << L"program is not running." << std::endl;
      }
      break;

    case STEP_OUT_COMMAND:
      if(interpreter) {
        is_step_out = true;
        jump_stack_pos = cur_call_stack_pos;
      }
      else {
        std::wcout << L"program is not running." << std::endl;
      }
      break;

    case CONT_COMMAND:
      if(interpreter) {
        continue_state = 1;
      }
      else {
        std::wcout << L"program is not running." << std::endl;
      }
      break;

    case MEMORY_COMMAND:
      if(interpreter) {
        const size_t alloc_mem = MemoryManager::GetAllocationSize();
        const size_t max_mem = MemoryManager::GetMaxMemory();
        std::wcout << L"memory: allocated=" << ToFloat(alloc_mem) << L", collection=" << ToFloat(max_mem) << std::endl;
      }
      else {
        std::wcout << L"program is not running." << std::endl;
      }
      break;

    case INFO_COMMAND:
      ProcessInfo(static_cast<Info*>(command));
      break;

    case STACK_COMMAND:
      if(interpreter) {
        std::wcout << L"stack:" << std::endl;
        StackMethod* method = cur_frame->method;
        std::wcout << L"  frame: pos=" << cur_call_stack_pos << L", class='" << method->GetClass()->GetName() << L"', method='" << PrintMethod(method) << L"'";
        const long ip = cur_frame->ip;
        if(ip > -1) {
          StackInstr* instr = cur_frame->method->GetInstruction(ip);
          std::wcout << L", file=" << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << std::endl;
        }
        else {
          std::wcout << std::endl;
        }

        long pos = cur_call_stack_pos;
        while(pos--) {
          StackMethod* method = cur_call_stack[pos]->method;
          if(method->GetClass()) {
            std::wcout << L"  frame: pos=" << pos << L", class='" << method->GetClass()->GetName() << L"', method='" << PrintMethod(method) << "'";
            const long ip = cur_call_stack[pos]->ip;
            if(ip > -1) {
              StackInstr* instr = cur_call_stack[pos]->method->GetInstruction(ip);
              std::wcout << L", file=" << method->GetClass()->GetFileName() << L":" << instr->GetLineNumber() << std::endl;
            }
            else {
              std::wcout << std::endl;
            }
          }
        }
        

      }
      else {
        std::wcout << L"program is not running." << std::endl;
      }
      break;

    case FRAME_COMMAND:
      ProcessFrame(static_cast<Frame*>(command));
      break;

    case UP_COMMAND:
      ProcessFrameShift(-1);
      break;

    case DOWN_COMMAND:
      ProcessFrameShift(1);
      break;

    case LOCALS_COMMAND:
      ProcessLocals();
      break;

    case SET_COMMAND:
      ProcessSet(static_cast<Set*>(command));
      break;

    case TBREAK_COMMAND:
      ProcessBreak(static_cast<FilePostion*>(command));
      break;

    case ENABLE_COMMAND:
      ProcessEnableDisable(static_cast<NumCommand*>(command)->GetId(), true);
      break;

    case DISABLE_COMMAND:
      ProcessEnableDisable(static_cast<NumCommand*>(command)->GetId(), false);
      break;

    case IGNORE_COMMAND: {
      NumCommand* num_command = static_cast<NumCommand*>(command);
      ProcessIgnore(num_command->GetId(), num_command->GetCount());
    }
      break;

    case UNTIL_COMMAND:
      ProcessUntil(static_cast<FilePostion*>(command));
      break;

    case WATCH_COMMAND:
      ProcessWatch(static_cast<Watch*>(command));
      break;

    case WATCHES_COMMAND:
      ProcessWatches();
      break;

    case UNWATCH_COMMAND:
      ProcessUnwatch(static_cast<Watch*>(command)->GetId());
      break;

    case HELP_COMMAND:
      std::wcout << L"\n" << C(CLR_BOLD) << L"Commands:" << C(CLR_RESET) << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"r, run" << C(CLR_RESET) << L"                  Start/restart program" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"b, break" << C(CLR_RESET) << L" <file>:<line> [if <expr>]  Set breakpoint" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"b, break" << C(CLR_RESET) << L" <Class>-><Method>  Break at method entry" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"tbreak" << C(CLR_RESET) << L" <file>:<line>    Temporary (one-shot) breakpoint" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"enable/disable" << C(CLR_RESET) << L" [<id>]   Enable/disable breakpoint(s)" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"ignore" << C(CLR_RESET) << L" <id> <count>     Skip next <count> hits of a breakpoint" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"watch" << C(CLR_RESET) << L" <expr>            Break when <expr> changes" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"watches" << C(CLR_RESET) << L"                 List watchpoints" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"unwatch" << C(CLR_RESET) << L" [<id>]          Remove watchpoint(s)" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"until" << C(CLR_RESET) << L" <line>            Run until <line> in the current frame" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"frame" << C(CLR_RESET) << L" [<n>]             Show/select stack frame" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"up / down" << C(CLR_RESET) << L"               Move to caller / callee frame" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"locals" << C(CLR_RESET) << L"                  Print all locals in the current frame" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"set" << C(CLR_RESET) << L" <var> = <expr>      Assign a new value to a variable" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"breaks" << C(CLR_RESET) << L"                  List all breakpoints" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"d, delete" << C(CLR_RESET) << L" <file>:<line>  Remove breakpoint" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"clear" << C(CLR_RESET) << L"                   Clear all breakpoints" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"c, cont" << C(CLR_RESET) << L"                 Continue execution" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"s, step" << C(CLR_RESET) << L"                 Step into" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"n, next" << C(CLR_RESET) << L"                 Step over" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"j, jump" << C(CLR_RESET) << L"                 Step out" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"p, print" << C(CLR_RESET) << L" <expr>         Print expression value" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"l, list" << C(CLR_RESET) << L" [<file>:<line>]  List source code" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"stack" << C(CLR_RESET) << L"                   Show call stack" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"m, memory" << C(CLR_RESET) << L"               Show memory stats" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"i, info" << C(CLR_RESET) << L" [class=<C>]     Show program/class info" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"exe" << C(CLR_RESET) << L" <file>              Load binary file" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"src" << C(CLR_RESET) << L" <dir>               Set source directory" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"args" << C(CLR_RESET) << L" '<args>'           Set program arguments" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"h, help" << C(CLR_RESET) << L"                 Show this help" << std::endl;
      std::wcout << L"  " << C(CLR_GREEN) << L"q, quit" << C(CLR_RESET) << L"                 Exit debugger" << std::endl;
      std::wcout << std::endl;
      break;

    default:
      break;
    }

    is_error = false;
    ref_mem = nullptr;
    return command;
  }
  else {
    std::wcout << C(CLR_RED) << L"-- Unable to process command --" << C(CLR_RESET) << std::endl;
  }

  is_error = false;
  ref_mem = nullptr;
  return nullptr;
}

void Runtime::Debugger::ProcessInfo(Info* info) {
  const std::wstring &cls_name = info->GetClassName();
  const std::wstring &mthd_name = info->GetMethodName();

#ifdef _DEBUG
  std::wcout << L"--- info class='" << cls_name << L"', method='" << mthd_name << L"' ---" << std::endl;
#endif

  if(interpreter) {
    // method info
    if(cls_name.size() > 0 && mthd_name.size() > 0) {
      StackClass* klass = cur_program->GetClass(cls_name);
      if(klass && klass->IsDebug()) {
        std::vector<StackMethod*> methods = klass->GetMethods(mthd_name);
        if(methods.size() > 0) {
          for(size_t i = 0; i < methods.size(); i++) {
            StackMethod* method = methods[i];
            std::wcout << L"  class: type=" << klass->GetName() << L", method="
                  << PrintMethod(method) << std::endl;
            if(method->GetNumberDeclarations() > 0) {
              std::wcout << L"  parameters:" << std::endl;
              PrintDeclarations(method->GetDeclarations(), method->GetNumberDeclarations());
            }
          }
        }
        else {
          std::wcout << L"unable to find method." << std::endl;
          is_error = true;
        }
      }
      else {
        std::wcout << L"unable to find class." << std::endl;
        is_error = true;
      }
    }
    // class info
    else if(cls_name.size() > 0) {
      StackClass* klass = cur_program->GetClass(cls_name);
      if(klass && klass->IsDebug()) {
        std::wcout << L"  class: type=" << klass->GetName() << std::endl;
        // print
        std::wcout << L"  parameters:" << std::endl;
        if(klass->GetNumberInstanceDeclarations() > 0) {
          PrintDeclarations(klass->GetInstanceDeclarations(), klass->GetNumberInstanceDeclarations());
          PrintDeclarations(klass->GetClassDeclarations(), klass->GetNumberClassDeclarations());
        }
      }
      else {
        std::wcout << L"unable to find class." << std::endl;
        is_error = true;
      }
    }
    // general info
    else {
      std::wcout << L"general info:" << std::endl;
      std::wcout << L"  program executable: file='" << program_file_param << L"'" << std::endl;

      // parse method and class names
      const std::wstring &long_name = cur_frame->method->GetName();
      size_t end_index = long_name.find_last_of(':');
      const std::wstring &cls_mthd_name = long_name.substr(0, end_index);

      size_t mid_index = cls_mthd_name.find_last_of(':');
      const std::wstring &cls_name = cls_mthd_name.substr(0, mid_index);
      const std::wstring &mthd_name = cls_mthd_name.substr(mid_index + 1);

      // print
      std::wcout << L"  current file='" << cur_file_name << L":" << cur_line_num << L"', method='"
            << cls_name << L"->" << mthd_name << L"(..)'" << std::endl;
    }
  }
  else {
    std::wcout << L"program is not running." << std::endl;
  }
}

void Runtime::Debugger::ClearBreaks() {
  std::wcout << L"breakpoints cleared." << std::endl;
  while(!breaks.empty()) {
    UserBreak* tmp = breaks.front();
    breaks.erase(breaks.begin());
    // delete
    delete tmp;
    tmp = nullptr;
  }
}

void Runtime::Debugger::ClearProgram(bool clear_loader) {
  if(clear_loader && loader) {
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

  if(loader) {
    MemoryManager::Clear();
  }

  is_step_into = is_next_line = is_step_out = false;
  continue_state = 0;
  cur_line_num = -1;
  cur_frame = nullptr;
  cur_method = nullptr;
  cur_program = nullptr;
  is_error = false;
  ref_mem = nullptr;
  ref_klass = nullptr;
  is_step_out = false;
  eval_frame = nullptr;
  eval_frame_pos = 0;
  until_line = -1;
  until_file.clear();
  prev_instr_line = -1;
  prev_instr_pos = -1;

  // clear watchpoints
  while(!watches.empty()) {
    WatchPoint* tmp = watches.front();
    watches.erase(watches.begin());
    delete tmp;
    tmp = nullptr;
  }
}

// ============================================
// DAP support methods
// ============================================

void Runtime::Debugger::DapRun() {
  if(!EndsWith(program_file_param, L".obe")) {
    program_file_param += L".obe";
  }

  if(FileExists(program_file_param, true) && DirectoryExists(base_path_param)) {
    arguments.clear();
    arguments.push_back(L"obr");
    arguments.push_back(program_file_param);

    if(!args_param.empty()) {
      ProcessArgs(args_param);
    }

    ClearReload();
    StackMethod* start = loader->GetStartMethod();
    if(start) {
      cur_file_name = start->GetClass()->GetFileName();
    }

    // Run the program
    DoLoad();
    cur_program = loader->GetProgram();
    // resolve any function breakpoints queued before load
    ResolvePendingMethodBreaks();

    op_stack = new size_t[CALC_STACK_SIZE];
    stack_pos = new size_t;
    (*stack_pos) = 0;

    interpreter = new Runtime::StackInterpreter(cur_program, this);
    interpreter->Execute(op_stack, stack_pos, 0, cur_program->GetInitializationMethod(), nullptr, false);

    ClearReload();
  }
}

void Runtime::Debugger::ClearFileBreaks(const std::wstring& file_name)
{
  auto iter = breaks.begin();
  while(iter != breaks.end()) {
    if((*iter)->file_name == file_name) {
      delete *iter;
      iter = breaks.erase(iter);
    }
    else {
      ++iter;
    }
  }
}

Expression* Runtime::Debugger::ParseCondition(const std::wstring& expr_str)
{
  // Use the parser to parse the expression
  std::wstring cmd = L"?p " + expr_str;
  Parser parser;
  Command* command = parser.Parse(cmd);
  if(command && command->GetCommandType() == PRINT_COMMAND) {
    Print* print = static_cast<Print*>(command);
    return print->GetExpression();
  }
  return nullptr;
}

// Format an object instance as "ClassName { field=val, ... }" for DAP hover/evaluate.
// Nested object fields show just the class name (one-level deep) to avoid recursion on cycles.
static std::wstring FormatObjectForDap(StackClass* klass, size_t* instance)
{
  if(!klass || !instance) {
    return L"Nil";
  }

  std::wstringstream wss;
  wss << klass->GetName() << L" {";
  StackDclr** inst_dclrs = klass->GetInstanceDeclarations();
  const int inst_num = klass->GetNumberInstanceDeclarations();
  int inst_idx = 0;
  for(int j = 0; j < inst_num; j++) {
    StackDclr* idclr = inst_dclrs[j];
    std::wstring iname = idclr->name;
    const size_t ipos = iname.find_last_of(':');
    if(ipos != std::wstring::npos) {
      iname = iname.substr(ipos + 1);
    }

    if(j > 0) {
      wss << L", ";
    }
    wss << iname << L"=";

    const size_t fval = instance[inst_idx];
    switch(idclr->type) {
      case CHAR_PARM:
        wss << (wchar_t)fval;
        break;
      case INT_PARM:
        wss << (long)fval;
        break;
      case FLOAT_PARM: {
        double dval;
        memcpy(&dval, &instance[inst_idx], sizeof(double));
        wss << dval;
        break;
      }
      case OBJ_PARM: {
        if(fval == 0) {
          wss << L"Nil";
        }
        else {
          StackClass* sub_klass = MemoryManager::GetClass((size_t*)fval);
          if(sub_klass) {
            wss << sub_klass->GetName();
          }
          else {
            wss << L"object@0x" << std::hex << fval << std::dec;
          }
        }
        break;
      }
      case BYTE_ARY_PARM:
      case CHAR_ARY_PARM:
      case INT_ARY_PARM:
      case FLOAT_ARY_PARM:
      case OBJ_ARY_PARM: {
        if(fval == 0) {
          wss << L"Nil";
        }
        else {
          size_t* arr = (size_t*)fval;
          wss << L"[size=" << arr[0] << L"]";
        }
        break;
      }
      case FUNC_PARM:
        wss << L"<function>";
        break;
      default:
        wss << L"<?>";
        break;
    }

    inst_idx++;
    // A Float is ONE slot in the 64-bit layout; only funcs take two.
    if(idclr->type == FUNC_PARM) {
      inst_idx++;
    }
  }
  wss << L"}";
  return wss.str();
}

std::wstring Runtime::Debugger::EvaluateForDap(const std::wstring& expr_str)
{
  if(!cur_frame) {
    return L"<no frame>";
  }

  // Try the CLI expression evaluator first
  std::wstring cmd = L"?p " + expr_str;
  Parser parser;
  Command* command = parser.Parse(cmd);
  if(command && command->GetCommandType() == PRINT_COMMAND) {
    Print* print = static_cast<Print*>(command);
    Expression* expression = print->GetExpression();
    is_error = false;
    EvaluateExpression(expression);

    if(!is_error) {
      const StackDclr& dclr = static_cast<Reference*>(expression)->GetDeclaration();
      switch(dclr.type) {
        case CHAR_PARM: {
          std::wstringstream wss;
          wss << (wchar_t)expression->GetIntValue();
          return wss.str();
        }
        case INT_PARM: {
          std::wstringstream wss;
          wss << (long)expression->GetIntValue();
          return wss.str();
        }
        case FLOAT_PARM: {
          std::wstringstream wss;
          wss << expression->GetFloatValue();
          return wss.str();
        }
        case OBJ_PARM: {
          size_t* instance = (size_t*)expression->GetIntValue();
          if(instance) {
            StackClass* klass = MemoryManager::GetClass(instance);
            if(klass) {
              return FormatObjectForDap(klass, instance);
            }
          }
          return L"Nil";
        }
        case BYTE_ARY_PARM:
        case CHAR_ARY_PARM:
        case INT_ARY_PARM:
        case FLOAT_ARY_PARM:
        case OBJ_ARY_PARM: {
          size_t* array = (size_t*)expression->GetIntValue();
          if(array) {
            std::wstringstream wss;
            wss << L"[size=" << array[0] << L"]";
            return wss.str();
          }
          return L"Nil";
        }
        case FUNC_PARM:
          return L"<function>";
        default:
          return L"<object>";
      }
    }
  }

  // Fallback: search frame declarations directly (handles inferred locals)
  if(cur_frame && cur_frame->method) {
    StackDclr** dclrs = cur_frame->method->GetDeclarations();
    int dclrs_num = cur_frame->method->GetNumberDeclarations();
    int offset = 1;
    if(cur_frame->method->HasAndOr()) {
      offset++;
    }

    int mem_index = 0;
    for(int i = 0; i < dclrs_num; i++) {
      StackDclr* dclr = dclrs[i];

      // Match variable name (strip class prefix)
      std::wstring full_name = dclr->name;
      size_t name_pos = full_name.find_last_of(':');
      std::wstring name = (name_pos != std::wstring::npos) ? full_name.substr(name_pos + 1) : full_name;

      if(name == expr_str) {
        size_t value = cur_frame->mem[mem_index + offset];

        switch(dclr->type) {
          case CHAR_PARM: {
            std::wstringstream wss;
            wss << (wchar_t)value;
            return wss.str();
          }
          case INT_PARM: {
            std::wstringstream wss;
            wss << (long)value;
            return wss.str();
          }
          case FLOAT_PARM: {
            double dval;
            memcpy(&dval, &cur_frame->mem[mem_index + offset], sizeof(double));
            std::wstringstream wss;
            wss << dval;
            return wss.str();
          }
          case OBJ_PARM: {
            if(value == 0) return L"Nil";
            size_t* instance = (size_t*)value;
            StackClass* klass = MemoryManager::GetClass(instance);
            if(klass) {
              return FormatObjectForDap(klass, instance);
            }
            std::wstringstream wss;
            wss << L"object@0x" << std::hex << value;
            return wss.str();
          }
          case BYTE_ARY_PARM:
          case CHAR_ARY_PARM:
          case INT_ARY_PARM:
          case FLOAT_ARY_PARM:
          case OBJ_ARY_PARM: {
            if(value == 0) return L"Nil";
            size_t* array = (size_t*)value;
            std::wstringstream wss;
            wss << L"[size=" << array[0] << L"]";
            return wss.str();
          }
          default:
            break;
        }
      }

      mem_index++;
      // A Float is ONE slot in the 64-bit layout; only funcs take two.
      if(dclr->type == FUNC_PARM) {
        mem_index++;
      }
    }
  }

  return L"<error>";
}

// ============================================
// CLI mode
// ============================================

void Runtime::Debugger::Debug() {
  ColorInit();

  std::wcout << C(CLR_CYAN) << L"-------------------------------------" << C(CLR_RESET) << std::endl;
  std::wcout << C(CLR_BOLD) << L"Objeck " << VERSION_STRING << L" - Interactive Debugger" << C(CLR_RESET) << std::endl;
  std::wcout << C(CLR_CYAN) << L"-------------------------------------" << C(CLR_RESET) << std::endl << std::endl;

  if(!EndsWith(program_file_param, L".obe")) {
    program_file_param += L".obe";
  }

  if(FileExists(program_file_param, true) && DirectoryExists(base_path_param)) {
    std::wcout << L"loaded binary: '" << program_file_param << L"'" << std::endl;
    std::wcout << L"source file path: '" << base_path_param << L"'" << std::endl << std::endl;

    // clear arguments
    arguments.clear();
    arguments.push_back(L"obr");
    arguments.push_back(program_file_param);
  }
  else {
    std::wcerr << L"unable to load executable='" << program_file_param << "' at locate base path='" << base_path_param << L"'" << std::endl;
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
    std::wstring line;
    ReadLine(line);
    // gdb-style: empty input repeats the previous command
    if(line.empty()) {
      line = last_cmd;
    }
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
    const std::wstring line = lines[i];
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
    std::wstring line = lines[i];
    const int line_size = (int)line.size();
    if(line_size > 0 && (line[0] == L' ' || line[0] == L'\t') && line_size > leading) {
      line = line.substr(leading);
    }

    const bool is_cur_line_num = i + 1 == cur_line_num;
    const bool is_break_point = debugger->FindBreak(i + 1);

    // Print line prefix (marker + line number) — ASCII only
    if(is_cur_line_num && is_break_point) {
      std::wcout << C(CLR_RED) << C(CLR_BOLD) << std::right << L"=>" << std::setw(window) << (i + 1) << L": " << C(CLR_RESET);
    }
    else if(is_cur_line_num) {
      std::wcout << C(CLR_YELLOW) << C(CLR_BOLD) << std::right << L"->" << std::setw(window) << (i + 1) << L": " << C(CLR_RESET);
    }
    else if(is_break_point) {
      std::wcout << C(CLR_RED) << std::right << L" #" << std::setw(window) << (i + 1) << L": " << C(CLR_RESET);
    }
    else {
      std::wcout << C(CLR_GRAY) << std::right << std::setw(window + 2) << (i + 1) << L": " << C(CLR_RESET);
    }
    std::wcout.flush();

    // Print source line content: use narrow stream on Windows for reliable
    // Unicode console output; use wcout on POSIX to avoid cout/wcout mixing
    // which causes buffering issues with expect-based test capture.
#ifdef _WIN32
    const std::string narrow_line = UnicodeToBytes(line);
    std::cout << narrow_line << std::endl;
    std::cout.flush();
#else
    std::wcout << line << std::endl;
#endif
  }

  std::wcout.flush();
  return true;
}
