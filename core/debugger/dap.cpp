/***************************************************************************
* Debug Adapter Protocol (DAP) implementation for the Objeck debugger
*
* Implements the DAP specification over stdin/stdout using JSON-RPC
* with Content-Length framing. Launched via: obd --dap
*
* Copyright (c) 2026, Randy Hollines
* All rights reserved.
*
* See LICENSE file for full copyright notice.
***************************************************************************/

#include "dap.h"

using namespace Runtime;

// ============================================
// Construction
// ============================================

DapAdapter::DapAdapter()
{
  debugger = nullptr;
  seq = 1;
  is_initialized = false;
  is_launched = false;
  is_terminated = false;
  is_stopped = false;
  resume_requested = false;
  step_into_requested = false;
  step_over_requested = false;
  step_out_requested = false;
  disconnect_requested = false;
  stopped_frame = nullptr;
  stopped_call_stack = nullptr;
  stopped_call_stack_pos = 0;
  stopped_line = 0;
}

DapAdapter::~DapAdapter()
{
  if(debugger) {
    delete debugger;
    debugger = nullptr;
  }
}

// ============================================
// JSON-RPC Transport
// ============================================

std::string DapAdapter::ReadMessage()
{
  // Read Content-Length header
  std::string header;
  while(true) {
    int ch = std::cin.get();
    if(ch == EOF) {
      return "";
    }
    header += (char)ch;
    if(header.size() >= 4 && header.substr(header.size() - 4) == "\r\n\r\n") {
      break;
    }
  }

  // Parse content length
  size_t pos = header.find("Content-Length: ");
  if(pos == std::string::npos) {
    return "";
  }
  int length = std::stoi(header.substr(pos + 16));

  // Read body
  std::string body(length, '\0');
  std::cin.read(&body[0], length);

  return body;
}

void DapAdapter::SendMessage(const json& msg)
{
  std::string body = msg.dump();
  std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  std::cout << header << body;
  std::cout.flush();
}

void DapAdapter::SendResponse(int request_seq, const std::string& command, const json& body, bool success, const std::string& message)
{
  json response;
  response["seq"] = seq++;
  response["type"] = "response";
  response["request_seq"] = request_seq;
  response["command"] = command;
  response["success"] = success;

  if(!body.empty()) {
    response["body"] = body;
  }

  if(!message.empty()) {
    response["message"] = message;
  }

  SendMessage(response);
}

void DapAdapter::SendEvent(const std::string& event, const json& body)
{
  json evt;
  evt["seq"] = seq++;
  evt["type"] = "event";
  evt["event"] = event;

  if(!body.empty()) {
    evt["body"] = body;
  }

  SendMessage(evt);
}

// ============================================
// Main DAP Message Loop
// ============================================

void DapAdapter::Run()
{
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  while(!disconnect_requested) {
    std::string msg_str = ReadMessage();
    if(msg_str.empty()) {
      break;
    }

    json msg;
    try {
      msg = json::parse(msg_str);
    }
    catch(...) {
      continue;
    }

    std::string command = msg.value("command", "");
    int request_seq = msg.value("seq", 0);
    json args = msg.value("arguments", json::object());

    if(command == "initialize") {
      HandleInitialize(request_seq, args);
    }
    else if(command == "launch") {
      HandleLaunch(request_seq, args);
    }
    else if(command == "setBreakpoints") {
      HandleSetBreakpoints(request_seq, args);
    }
    else if(command == "configurationDone") {
      HandleConfigurationDone(request_seq, args);
    }
    else if(command == "threads") {
      HandleThreads(request_seq);
    }
    else if(command == "stackTrace") {
      HandleStackTrace(request_seq, args);
    }
    else if(command == "scopes") {
      HandleScopes(request_seq, args);
    }
    else if(command == "variables") {
      HandleVariables(request_seq, args);
    }
    else if(command == "continue") {
      HandleContinue(request_seq, args);
    }
    else if(command == "next") {
      HandleNext(request_seq, args);
    }
    else if(command == "stepIn") {
      HandleStepIn(request_seq, args);
    }
    else if(command == "stepOut") {
      HandleStepOut(request_seq, args);
    }
    else if(command == "pause") {
      HandlePause(request_seq, args);
    }
    else if(command == "disconnect") {
      HandleDisconnect(request_seq, args);
    }
    else if(command == "evaluate") {
      HandleEvaluate(request_seq, args);
    }
    else {
      // Unknown command — respond with success to avoid VS Code errors
      SendResponse(request_seq, command);
    }
  }
}

// ============================================
// DAP Request Handlers
// ============================================

void DapAdapter::HandleInitialize(int request_seq, const json& args)
{
  json capabilities;
  capabilities["supportsConditionalBreakpoints"] = true;
  capabilities["supportsConfigurationDoneRequest"] = true;
  capabilities["supportsEvaluateForHovers"] = false;
  capabilities["supportsStepBack"] = false;
  capabilities["supportsSetVariable"] = false;
  capabilities["supportsFunctionBreakpoints"] = false;
  capabilities["supportsRestartRequest"] = false;

  SendResponse(request_seq, "initialize", capabilities);

  // Send initialized event
  SendEvent("initialized");
  is_initialized = true;
}

void DapAdapter::HandleLaunch(int request_seq, const json& args)
{
  // Extract launch parameters
  std::string program = args.value("program", "");
  std::string source = args.value("sourceDir", ".");
  std::string prog_args = args.value("args", "");

  if(program.empty()) {
    SendResponse(request_seq, "launch", json::object(), false, "No program specified");
    return;
  }

  program_path = BytesToUnicode(program);
  source_dir = BytesToUnicode(source);
  program_args = BytesToUnicode(prog_args);

  // Ensure source dir ends with separator
#ifdef _WIN32
  if(source_dir.size() > 0 && source_dir.back() != L'\\') {
    source_dir += L'\\';
  }
#else
  if(source_dir.size() > 0 && source_dir.back() != L'/') {
    source_dir += L'/';
  }
#endif

  // Create debugger instance
  debugger = new Debugger(program_path, source_dir, program_args);
  debugger->SetDapAdapter(this);

  is_launched = true;
  SendResponse(request_seq, "launch");

  // Send thread started event
  json thread_body;
  thread_body["reason"] = "started";
  thread_body["threadId"] = 1;
  SendEvent("thread", thread_body);
}

void DapAdapter::HandleSetBreakpoints(int request_seq, const json& args)
{
  json source = args.value("source", json::object());
  std::string path = source.value("path", "");
  json breakpoints_json = args.value("breakpoints", json::array());

  // Extract just the filename from the path
  std::string file_name = path;
  size_t sep_pos = path.find_last_of("/\\");
  if(sep_pos != std::string::npos) {
    file_name = path.substr(sep_pos + 1);
  }

  std::wstring wfile_name = BytesToUnicode(file_name);

  // Clear existing breakpoints for this file
  if(debugger) {
    debugger->ClearFileBreaks(wfile_name);
  }

  // Add new breakpoints
  json verified_breakpoints = json::array();
  for(const auto& bp : breakpoints_json) {
    int line = bp.value("line", 0);
    std::string condition_str = bp.value("condition", "");

    bool added = false;
    if(debugger && line > 0) {
      // Parse condition if provided
      Expression* condition = nullptr;
      if(!condition_str.empty()) {
        std::wstring wcond = BytesToUnicode(condition_str);
        condition = debugger->ParseCondition(wcond);
      }
      added = debugger->AddBreak(line, wfile_name, condition);
    }

    json bp_result;
    bp_result["id"] = line;
    bp_result["line"] = line;
    bp_result["verified"] = added;
    verified_breakpoints.push_back(bp_result);
  }

  json body;
  body["breakpoints"] = verified_breakpoints;
  SendResponse(request_seq, "setBreakpoints", body);
}

void DapAdapter::HandleConfigurationDone(int request_seq, const json& args)
{
  SendResponse(request_seq, "configurationDone");

  // Start the program on a background thread
  if(debugger && is_launched) {
    std::thread vm_thread([this]() {
      debugger->DapRun();
      // Program finished
      if(!disconnect_requested) {
        OnTerminated();
      }
    });
    vm_thread.detach();
  }
}

void DapAdapter::HandleThreads(int request_seq)
{
  // Objeck is single-threaded from the debugger's perspective
  json body;
  json threads = json::array();
  json thread;
  thread["id"] = 1;
  thread["name"] = "Main Thread";
  threads.push_back(thread);
  body["threads"] = threads;
  SendResponse(request_seq, "threads", body);
}

std::string DapAdapter::ResolveSourcePath(const std::wstring& file_name)
{
  std::wstring combined = source_dir + file_name;
  std::string raw = UnicodeToBytes(combined);

#ifdef _WIN32
  char resolved[_MAX_PATH];
  if(_fullpath(resolved, raw.c_str(), _MAX_PATH)) {
    return std::string(resolved);
  }
#else
  char resolved[PATH_MAX];
  if(realpath(raw.c_str(), resolved)) {
    return std::string(resolved);
  }
#endif

  return raw;
}

void DapAdapter::HandleStackTrace(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  json frames = json::array();

  if(is_stopped && stopped_frame) {
    // Current frame
    StackMethod* method = stopped_frame->method;
    if(method && method->GetClass()) {
      json frame;
      frame["id"] = (int)stopped_call_stack_pos;
      frame["name"] = UnicodeToBytes(debugger->PrintMethodPublic(method));

      json source;
      std::string file = UnicodeToBytes(method->GetClass()->GetFileName());
      source["name"] = file;
      source["path"] = ResolveSourcePath(method->GetClass()->GetFileName());
      frame["source"] = source;
      frame["line"] = stopped_line;
      frame["column"] = 1;
      frames.push_back(frame);
    }

    // Walk call stack
    long pos = stopped_call_stack_pos;
    while(pos--) {
      StackMethod* method = stopped_call_stack[pos]->method;
      if(method && method->GetClass()) {
        json frame;
        frame["id"] = (int)pos;
        frame["name"] = UnicodeToBytes(debugger->PrintMethodPublic(method));

        json source;
        source["name"] = UnicodeToBytes(method->GetClass()->GetFileName());
        source["path"] = ResolveSourcePath(method->GetClass()->GetFileName());
        frame["source"] = source;

        long ip = stopped_call_stack[pos]->ip;
        if(ip > -1) {
          StackInstr* instr = method->GetInstruction(ip);
          frame["line"] = instr->GetLineNumber();
        }
        else {
          frame["line"] = 0;
        }
        frame["column"] = 1;
        frames.push_back(frame);
      }
    }
  }

  json body;
  body["stackFrames"] = frames;
  body["totalFrames"] = (int)frames.size();
  SendResponse(request_seq, "stackTrace", body);
}

void DapAdapter::HandleScopes(int request_seq, const json& args)
{
  int frame_id = args.value("frameId", 0);

  json scopes_arr = json::array();
  json local_scope;
  local_scope["name"] = "Locals";
  local_scope["presentationHint"] = "locals";
  local_scope["variablesReference"] = SCOPE_HANDLE_BASE + frame_id;
  local_scope["expensive"] = false;
  scopes_arr.push_back(local_scope);

  json body;
  body["scopes"] = scopes_arr;
  SendResponse(request_seq, "scopes", body);
}

void DapAdapter::HandleVariables(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  int ref = args.value("variablesReference", 0);
  json variables = json::array();

  if(is_stopped && ref >= SCOPE_HANDLE_BASE && ref < VAR_HANDLE_BASE) {
    // Scope reference — enumerate locals
    int frame_index = ref - SCOPE_HANDLE_BASE;
    StackFrame* frame = nullptr;

    if(frame_index == (int)stopped_call_stack_pos) {
      frame = stopped_frame;
    }
    else if(frame_index >= 0 && frame_index < (int)stopped_call_stack_pos) {
      frame = stopped_call_stack[frame_index];
    }

    if(frame && frame->method) {
      StackDclr** dclrs = frame->method->GetDeclarations();
      int dclrs_num = frame->method->GetNumberDeclarations();

      for(int i = 0; i < dclrs_num; i++) {
        StackDclr* dclr = dclrs[i];

        // Parse variable name (strip class prefix)
        std::wstring full_name = dclr->name;
        size_t name_index = full_name.find_last_of(':');
        std::string name;
        if(name_index != std::wstring::npos) {
          name = UnicodeToBytes(full_name.substr(name_index + 1));
        }
        else {
          name = UnicodeToBytes(full_name);
        }

        json var;
        var["name"] = name;
        var["value"] = FormatVariableValue(*dclr, frame, i);
        var["type"] = FormatVariableType(*dclr);
        var["variablesReference"] = 0;
        variables.push_back(var);
      }
    }
  }

  json body;
  body["variables"] = variables;
  SendResponse(request_seq, "variables", body);
}

void DapAdapter::HandleContinue(int request_seq, const json& args)
{
  SendResponse(request_seq, "continue", {{"allThreadsContinued", true}});

  std::lock_guard<std::mutex> lock(mtx);
  step_into_requested = false;
  step_over_requested = false;
  step_out_requested = false;
  resume_requested = true;
  is_stopped = false;
  cv.notify_all();
}

void DapAdapter::HandleNext(int request_seq, const json& args)
{
  SendResponse(request_seq, "next");

  std::lock_guard<std::mutex> lock(mtx);
  step_into_requested = false;
  step_over_requested = true;
  step_out_requested = false;
  resume_requested = true;
  is_stopped = false;
  cv.notify_all();
}

void DapAdapter::HandleStepIn(int request_seq, const json& args)
{
  SendResponse(request_seq, "stepIn");

  std::lock_guard<std::mutex> lock(mtx);
  step_into_requested = true;
  step_over_requested = false;
  step_out_requested = false;
  resume_requested = true;
  is_stopped = false;
  cv.notify_all();
}

void DapAdapter::HandleStepOut(int request_seq, const json& args)
{
  SendResponse(request_seq, "stepOut");

  std::lock_guard<std::mutex> lock(mtx);
  step_into_requested = false;
  step_over_requested = false;
  step_out_requested = true;
  resume_requested = true;
  is_stopped = false;
  cv.notify_all();
}

void DapAdapter::HandlePause(int request_seq, const json& args)
{
  // Not fully supported — respond success but no action
  SendResponse(request_seq, "pause");
}

void DapAdapter::HandleDisconnect(int request_seq, const json& args)
{
  disconnect_requested = true;

  // If stopped, resume so the VM thread can exit
  {
    std::lock_guard<std::mutex> lock(mtx);
    resume_requested = true;
    is_stopped = false;
    cv.notify_all();
  }

  SendResponse(request_seq, "disconnect");
}

void DapAdapter::HandleEvaluate(int request_seq, const json& args)
{
  std::string expression = args.value("expression", "");

  if(expression.empty() || !is_stopped) {
    SendResponse(request_seq, "evaluate", json::object(), false, "Cannot evaluate");
    return;
  }

  // Use the debugger's expression evaluator
  std::wstring wexpr = BytesToUnicode(expression);
  std::wstring result = debugger->EvaluateForDap(wexpr);

  json body;
  body["result"] = UnicodeToBytes(result);
  body["variablesReference"] = 0;
  SendResponse(request_seq, "evaluate", body);
}

// ============================================
// Callbacks from Debugger
// ============================================

void DapAdapter::OnStopped(const std::string& reason, int line, const std::wstring& file,
                           StackFrame* frame, StackFrame** call_stack, long call_stack_pos)
{
  {
    std::lock_guard<std::mutex> lock(mtx);
    is_stopped = true;
    resume_requested = false;
    step_into_requested = false;
    step_over_requested = false;
    step_out_requested = false;
    stopped_line = line;
    stopped_file = file;
    stop_reason = reason;
    stopped_frame = frame;
    stopped_call_stack = call_stack;
    stopped_call_stack_pos = call_stack_pos;
  }

  // Send stopped event
  json body;
  body["reason"] = reason;
  body["threadId"] = 1;
  body["allThreadsStopped"] = true;
  SendEvent("stopped", body);

  // Block the VM thread until DAP sends continue/step
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [this]() { return resume_requested || disconnect_requested; });
}

void DapAdapter::OnTerminated()
{
  if(!is_terminated) {
    is_terminated = true;
    SendEvent("terminated");

    json thread_body;
    thread_body["reason"] = "exited";
    thread_body["threadId"] = 1;
    SendEvent("thread", thread_body);
  }
}

// ============================================
// Variable Formatting
// ============================================

std::string DapAdapter::FormatVariableValue(StackDclr& dclr, StackFrame* frame, int var_index)
{
  if(!frame || !frame->mem) {
    return "<unavailable>";
  }

  size_t value = frame->mem[var_index];

  switch(dclr.type) {
    case CHAR_PARM:
      return std::string(1, (char)(wchar_t)value);

    case INT_PARM: {
      std::ostringstream oss;
      oss << (long)value;
      return oss.str();
    }

    case FLOAT_PARM: {
      double dval;
      memcpy(&dval, &value, sizeof(double));
      std::ostringstream oss;
      oss << dval;
      return oss.str();
    }

    case BYTE_ARY_PARM:
    case CHAR_ARY_PARM:
    case INT_ARY_PARM:
    case FLOAT_ARY_PARM:
    case OBJ_ARY_PARM: {
      if(value == 0) {
        return "Nil";
      }
      size_t* array = (size_t*)value;
      std::ostringstream oss;
      oss << "[size=" << array[0] << "]";
      return oss.str();
    }

    case OBJ_PARM: {
      if(value == 0) {
        return "Nil";
      }
      // Try to get a meaningful representation
      std::ostringstream oss;
      oss << "object@0x" << std::hex << value;
      return oss.str();
    }

    case FUNC_PARM:
      return "<function>";

    default:
      return "<unknown>";
  }
}

std::string DapAdapter::FormatVariableType(StackDclr& dclr)
{
  switch(dclr.type) {
    case CHAR_PARM:
      return "Char";
    case INT_PARM:
      return "Int";
    case FLOAT_PARM:
      return "Float";
    case BYTE_ARY_PARM:
      return "Byte[]";
    case CHAR_ARY_PARM:
      return "Char[]";
    case INT_ARY_PARM:
      return "Int[]";
    case FLOAT_ARY_PARM:
      return "Float[]";
    case OBJ_PARM:
      return "Object";
    case OBJ_ARY_PARM:
      return "Object[]";
    case FUNC_PARM:
      return "Function";
    default:
      return "unknown";
  }
}
