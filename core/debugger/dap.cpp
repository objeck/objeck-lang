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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <windows.h>
#define DUP _dup
#define DUP2 _dup2
#define READ _read
#define WRITE _write
#define CLOSE _close
#define PIPE(fds) _pipe(fds, 4096, _O_BINARY)
#define FILENO _fileno
#else
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#define DUP dup
#define DUP2 dup2
#define READ read
#define WRITE write
#define CLOSE close
#define PIPE(fds) pipe(fds)
#define FILENO fileno
#endif

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
  restart_requested = false;
  break_on_exception = false;
  client_supports_variable_paging = false;
  stopped_frame = nullptr;
  stopped_call_stack = nullptr;
  stopped_call_stack_pos = 0;
  stopped_line = 0;
  dap_in_fd = -1;
  dap_out_fd = -1;
  prog_stdout_pipe[0] = prog_stdout_pipe[1] = -1;
  prog_stderr_pipe[0] = prog_stderr_pipe[1] = -1;
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
  // Read Content-Length header from the saved DAP input fd. We must
  // not use std::cin here because the underlying fd may be the same as
  // the program's redirected stdin in some configurations; the dup'd
  // dap_in_fd is the canonical channel.
  std::string header;
  while(true) {
    char ch;
    int n = READ(dap_in_fd, &ch, 1);
    if(n <= 0) {
      return "";
    }
    header += ch;
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
  int total = 0;
  while(total < length) {
    int n = READ(dap_in_fd, &body[total], length - total);
    if(n <= 0) {
      return "";
    }
    total += n;
  }

  return body;
}

void DapAdapter::SendMessage(const json& msg)
{
  // Serialize against output reader threads which also write framed
  // messages to dap_out_fd. Without this lock, the headers and bodies
  // of concurrent sends would interleave on the wire.
  std::lock_guard<std::mutex> lock(send_mtx);

  std::string body = msg.dump();
  std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  WRITE(dap_out_fd, header.data(), (unsigned int)header.size());
  WRITE(dap_out_fd, body.data(), (unsigned int)body.size());
}

// ============================================
// Stdio redirection (program stdout/stderr -> DAP output events)
// ============================================

void DapAdapter::RedirectProgramStdio()
{
  // Save the real DAP fds before we touch process stdio. After this
  // function, std::cout / std::cerr / printf / wprintf will all flow
  // into the capture pipes; only writes to dap_out_fd / dap_in_fd reach
  // the editor's DAP transport.
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  dap_out_fd = DUP(FILENO(stdout));
  dap_in_fd = DUP(FILENO(stdin));

  // Capture program stdout
  if(PIPE(prog_stdout_pipe) == 0) {
    std::wcout.flush();
    std::cout.flush();
    fflush(stdout);
    DUP2(prog_stdout_pipe[1], FILENO(stdout));
    CLOSE(prog_stdout_pipe[1]);
    prog_stdout_pipe[1] = -1;
    // unitbuf forces std::wcout / std::cout to flush after every
    // insertion. The C++ wide stream has its own buffer separate from
    // the C FILE*, so without unitbuf the user program's PrintLine
    // output sits in std::wcout's buffer and never reaches the pipe
    // until process exit.
    std::cout.setf(std::ios::unitbuf);
    std::wcout.setf(std::ios::unitbuf);
  }

  // Capture program stderr
  if(PIPE(prog_stderr_pipe) == 0) {
    std::wcerr.flush();
    std::cerr.flush();
    fflush(stderr);
    DUP2(prog_stderr_pipe[1], FILENO(stderr));
    CLOSE(prog_stderr_pipe[1]);
    prog_stderr_pipe[1] = -1;
    std::cerr.setf(std::ios::unitbuf);
    std::wcerr.setf(std::ios::unitbuf);
  }
}

void DapAdapter::OutputReaderLoop(int fd, const std::string& category)
{
  // Read raw bytes from the program's redirected stdio pipe and forward
  // each chunk as a DAP `output` event. On Windows the VM writes to
  // std::wcout, so the bytes in the pipe are UTF-16 LE wide chars; we
  // decode them to UTF-8 before sending the event so the editor displays
  // text correctly. On POSIX std::wcout converts to the locale encoding
  // (UTF-8 in modern setups), so we forward bytes as-is.
  char buf[2048];
#ifdef _WIN32
  // `pending` holds the trailing odd byte from the previous read so we
  // never split a UTF-16 code unit across DAP events.
  char pending = 0;
  bool has_pending = false;
#endif

  while(true) {
    int n = READ(fd, buf, sizeof(buf));
    if(n <= 0) {
      // Pipe closed (program ended) or error — exit cleanly.
      return;
    }

#ifdef _WIN32
    // Combine the saved odd byte from the previous read with the new
    // chunk so the wide-char count is always whole.
    char chunk[sizeof(buf) + 1];
    int chunk_len = 0;
    if(has_pending) {
      chunk[chunk_len++] = pending;
      has_pending = false;
    }
    memcpy(chunk + chunk_len, buf, n);
    chunk_len += n;

    int wlen = chunk_len / 2;
    int leftover = chunk_len & 1;
    if(leftover) {
      pending = chunk[chunk_len - 1];
      has_pending = true;
    }

    if(wlen > 0) {
      const wchar_t* wide = (const wchar_t*)chunk;
      int ulen = WideCharToMultiByte(CP_UTF8, 0, wide, wlen, nullptr, 0, nullptr, nullptr);
      if(ulen > 0) {
        std::string utf8(ulen, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide, wlen, &utf8[0], ulen, nullptr, nullptr);
        json body;
        body["category"] = category;
        body["output"] = utf8;
        SendEvent("output", body);
      }
    }
#else
    json body;
    body["category"] = category;
    body["output"] = std::string(buf, n);
    SendEvent("output", body);
#endif
  }
}

void DapAdapter::StartOutputReaders()
{
  if(prog_stdout_pipe[0] >= 0) {
    int fd = prog_stdout_pipe[0];
    stdout_reader_thread = std::thread([this, fd]() {
      this->OutputReaderLoop(fd, "stdout");
    });
    stdout_reader_thread.detach();
  }
  if(prog_stderr_pipe[0] >= 0) {
    int fd = prog_stderr_pipe[0];
    stderr_reader_thread = std::thread([this, fd]() {
      this->OutputReaderLoop(fd, "stderr");
    });
    stderr_reader_thread.detach();
  }
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
  // Save real stdin/stdout and redirect process stdout/stderr to pipes
  // BEFORE we touch any DAP I/O. From this point on, std::cout and the
  // VM's print opcodes write to the capture pipes; only DAP framed
  // messages (via SendMessage / ReadMessage) reach the editor.
  RedirectProgramStdio();
  StartOutputReaders();

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
    else if(command == "setVariable") {
      HandleSetVariable(request_seq, args);
    }
    else if(command == "setFunctionBreakpoints") {
      HandleSetFunctionBreakpoints(request_seq, args);
    }
    else if(command == "setExceptionBreakpoints") {
      HandleSetExceptionBreakpoints(request_seq, args);
    }
    else if(command == "restart") {
      HandleRestart(request_seq, args);
    }
    else if(command == "terminate") {
      HandleTerminate(request_seq, args);
    }
    else if(command == "breakpointLocations") {
      HandleBreakpointLocations(request_seq, args);
    }
    else if(command == "exceptionInfo") {
      HandleExceptionInfo(request_seq, args);
    }
    else if(command == "setExpression") {
      HandleSetExpression(request_seq, args);
    }
    else if(command == "completions") {
      HandleCompletions(request_seq, args);
    }
    else if(command == "modules") {
      HandleModules(request_seq, args);
    }
    else if(command == "loadedSources") {
      HandleLoadedSources(request_seq, args);
    }
    else if(command == "dataBreakpointInfo") {
      HandleDataBreakpointInfo(request_seq, args);
    }
    else if(command == "setDataBreakpoints") {
      HandleSetDataBreakpoints(request_seq, args);
    }
    else {
      // Unknown command — respond with success to avoid VS Code errors
      SendResponse(request_seq, command);
    }
  }

  // Wait for the VM thread to finish before tearing anything down.
  // The VM checks IsDisconnected() at every breakpoint check, so once
  // disconnect_requested is true the VM will exit DapRun() within at
  // most one bytecode dispatch (typically microseconds). Joining
  // (rather than detaching) eliminates the race where the VM is still
  // executing while the C runtime tears down its state, which used to
  // cause an access violation (0xC0000005) on disconnect.
  if(vm_thread.joinable()) {
    vm_thread.join();
  }

  // Restore the original stdout/stderr fds before returning so the C
  // runtime's static cleanup at process exit doesn't try to flush a
  // closed pipe (which triggers Windows __fastfail / 0xC0000409). The
  // output reader threads will see EOF on the pipe and exit cleanly.
  if(dap_out_fd >= 0) {
    fflush(stdout);
    DUP2(dap_out_fd, FILENO(stdout));
    fflush(stderr);
    DUP2(dap_out_fd, FILENO(stderr));
  }
}

// ============================================
// DAP Request Handlers
// ============================================

void DapAdapter::HandleInitialize(int request_seq, const json& args)
{
  // Client capabilities travel on the initialize request; remember the ones
  // that change what we may send back.
  client_supports_variable_paging = args.value("supportsVariablePaging", false);

  json capabilities;
  capabilities["supportsConditionalBreakpoints"] = true;
  capabilities["supportsConfigurationDoneRequest"] = true;
  capabilities["supportsEvaluateForHovers"] = true;
  capabilities["supportsStepBack"] = false;
  capabilities["supportsSetVariable"] = true;
  capabilities["supportsFunctionBreakpoints"] = true;
  capabilities["supportsRestartRequest"] = true;
  capabilities["supportsLogPoints"] = true;
  capabilities["supportsTerminateRequest"] = true;
  capabilities["supportsBreakpointLocationsRequest"] = true;
  capabilities["supportsExceptionInfoRequest"] = true;
  capabilities["supportsSetExpression"] = true;
  capabilities["supportsModulesRequest"] = true;
  capabilities["supportsLoadedSourcesRequest"] = true;
  capabilities["supportsDataBreakpoints"] = true;
  capabilities["supportsCompletionsRequest"] = true;

  // Offer completions as soon as a word or an instance-variable sigil starts.
  capabilities["completionTriggerCharacters"] = json::array({"@", "."});

  // exception breakpoints: break on an uncaught Objeck runtime error
  json filter;
  filter["filter"] = "uncaught";
  filter["label"] = "Uncaught Runtime Errors";
  filter["default"] = false;
  capabilities["exceptionBreakpointFilters"] = json::array({filter});

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
    std::string log_message_str = bp.value("logMessage", "");

    bool added = false;
    if(debugger && line > 0) {
      if(!log_message_str.empty()) {
        // logpoint: log-and-continue instead of stopping
        added = debugger->AddLogPoint(line, wfile_name, BytesToUnicode(log_message_str));
      }
      else {
        // Parse condition if provided
        Expression* condition = nullptr;
        if(!condition_str.empty()) {
          std::wstring wcond = BytesToUnicode(condition_str);
          condition = debugger->ParseCondition(wcond);
        }
        added = debugger->AddBreak(line, wfile_name, condition);
      }
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

  // Start the program on a background thread. Store the thread as a
  // member so Run() can join it on shutdown — detaching would let the
  // VM race the C runtime teardown and access-violate.
  if(debugger && is_launched) {
    vm_thread = std::thread([this]() {
      // Re-run on restart from this same thread. The current run is halted
      // cleanly (RequestHalt via the disconnect path), so a fresh DapRun
      // re-loads the program from scratch — equivalent to a CLI re-run.
      bool again = true;
      while(again) {
        debugger->DapRun();

        // Decide whether to loop under the lock so a restart request that
        // arrives while DapRun is unwinding isn't lost or double-counted.
        std::lock_guard<std::mutex> lock(mtx);
        again = restart_requested;
        restart_requested = false;
        if(again) {
          // clear the stop flags for the fresh run
          disconnect_requested = false;
          is_stopped = false;
          is_terminated = false;
        }
      }

      // Program finished naturally — only fire `terminated` if the user
      // didn't already request disconnect (otherwise the editor sees a
      // late terminated event after it expects us to be gone).
      if(!disconnect_requested) {
        OnTerminated();
      }
    });
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
  // Extract basename from file_name
  std::wstring basename = file_name;
  size_t sep = file_name.find_last_of(L"/\\");
  if(sep != std::wstring::npos) {
    basename = file_name.substr(sep + 1);
  }

  // Try source_dir + basename first
  std::wstring combined = source_dir + basename;
  std::string candidate = UnicodeToBytes(combined);

#ifdef _WIN32
  char resolved[_MAX_PATH];
  if(_fullpath(resolved, candidate.c_str(), _MAX_PATH)) {
    // Verify file exists
    if(GetFileAttributesA(resolved) != INVALID_FILE_ATTRIBUTES) {
      return std::string(resolved);
    }
  }
#else
  char resolved[PATH_MAX];
  if(realpath(candidate.c_str(), resolved)) {
    return std::string(resolved);
  }
#endif

  return candidate;
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
      std::string resolved = ResolveSourcePath(method->GetClass()->GetFileName());
      // Extract basename for display name
      std::string basename = resolved;
      size_t lastSep = resolved.find_last_of("/\\");
      if(lastSep != std::string::npos) basename = resolved.substr(lastSep + 1);
      source["name"] = basename;
      source["path"] = resolved;
      source["sourceReference"] = 0;
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
        std::string resolved2 = ResolveSourcePath(method->GetClass()->GetFileName());
        std::string basename2 = resolved2;
        size_t lastSep2 = resolved2.find_last_of("/\\");
        if(lastSep2 != std::string::npos) basename2 = resolved2.substr(lastSep2 + 1);
        source["name"] = basename2;
        source["path"] = resolved2;
        source["sourceReference"] = 0;
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
  std::lock_guard<std::mutex> lock(mtx);
  int frame_id = args.value("frameId", 0);

  json scopes_arr = json::array();
  json local_scope;
  local_scope["name"] = "Locals";
  local_scope["presentationHint"] = "locals";
  local_scope["variablesReference"] = SCOPE_HANDLE_BASE + frame_id;
  local_scope["expensive"] = false;
  scopes_arr.push_back(local_scope);

  if(is_stopped) {
    StackFrame* frame = GetFrameByIndex(frame_id);
    if(frame && frame->method) {
      StackClass* klass = frame->method->GetClass();
      if(klass) {
        // Instance variables scope (only for instance methods with valid @self)
        if(klass->GetNumberInstanceDeclarations() > 0 && frame->mem && frame->mem[0] &&
           MemoryManager::GetClass((size_t*)frame->mem[0])) {
          json inst_scope;
          inst_scope["name"] = "Instance";
          inst_scope["variablesReference"] = INST_SCOPE_HANDLE_BASE + frame_id;
          inst_scope["expensive"] = false;
          scopes_arr.push_back(inst_scope);
        }
        // Class variables scope
        if(klass->GetNumberClassDeclarations() > 0 && klass->GetClassMemory()) {
          json cls_scope;
          cls_scope["name"] = "Class";
          cls_scope["variablesReference"] = CLS_SCOPE_HANDLE_BASE + frame_id;
          cls_scope["expensive"] = false;
          scopes_arr.push_back(cls_scope);
        }
      }
    }
  }

  json body;
  body["scopes"] = scopes_arr;
  SendResponse(request_seq, "scopes", body);
}

// Declarations are stored fully qualified ("Class:method:name"); clients want
// just the trailing name.
std::string DapAdapter::ShortDeclarationName(const std::wstring& full_name)
{
  const size_t name_index = full_name.find_last_of(L':');
  return UnicodeToBytes(name_index == std::wstring::npos ? full_name : full_name.substr(name_index + 1));
}

void DapAdapter::ClearVarHandles()
{
  var_handles.clear();
}

int DapAdapter::AllocVarHandle(int kind, size_t* ptr, StackClass* klass, int elem_type, int depth)
{
  // Depth and table caps keep a cyclic structure (a Vector holding itself)
  // from expanding forever.
  if(!ptr || depth >= MAX_EXPANSION_DEPTH || var_handles.size() >= MAX_VAR_HANDLES) {
    return 0;
  }

  VarHandle handle;
  handle.kind = kind;
  handle.ptr = ptr;
  handle.klass = klass;
  handle.elem_type = elem_type;
  handle.depth = depth;
  var_handles.push_back(handle);

  return DYN_HANDLE_BASE + (int)var_handles.size() - 1;
}

// Returns a child handle for anything worth drilling into, or 0 for a leaf.
int DapAdapter::MakeChildRef(ParamType type, size_t raw_value, int depth)
{
  if(raw_value == 0) {
    return 0;
  }

  switch(type) {
  case OBJ_PARM: {
    StackClass* klass = MemoryManager::GetClass((size_t*)raw_value);
    if(!klass) {
      return 0;
    }
    // A string or a boxed scalar already shows its whole value; expanding it
    // into a backing array and capacity fields is just noise.
    if(IsLeafObject(klass)) {
      return 0;
    }
    return AllocVarHandle(CollectionKind(klass), (size_t*)raw_value, klass, 0, depth);
  }

  case INT_ARY_PARM:
  case FLOAT_ARY_PARM:
  case OBJ_ARY_PARM:
    return AllocVarHandle(VAR_ARRAY, (size_t*)raw_value, nullptr, (int)type, depth);

    // Char arrays already render as a string preview, and byte arrays are
    // packed rather than one element per slot; neither is expanded.
  default:
    return 0;
  }
}

// Locates an instance field by its short name (declarations are stored
// fully qualified, e.g. "Collection.Vector:@values") and reports the memory
// slot it occupies. Looking fields up by name rather than by a hard-coded
// slot keeps this working if a library class gains or reorders fields.
// Library classes are compiled without debug symbols, so their declaration
// names are blank and the lookup below cannot find them. For the handful of
// collection internals we walk, fall back to the slot the field occupies in
// the library source. The index is validated against the declaration count so
// a layout change degrades to generic expansion instead of reading garbage.
bool DapAdapter::FieldIndex(StackClass* klass, const std::wstring& short_name, int fallback_index, int& out_index)
{
  if(FindInstanceField(klass, short_name, out_index)) {
    return true;
  }

  if(klass && fallback_index >= 0 && fallback_index < (int)klass->GetNumberInstanceDeclarations()) {
    out_index = fallback_index;
    return true;
  }

  out_index = 0;
  return false;
}

bool DapAdapter::FindInstanceField(StackClass* klass, const std::wstring& short_name, int& out_index)
{
  out_index = 0;

  if(!klass || short_name.empty()) {
    return false;
  }

  StackDclr** dclrs = klass->GetInstanceDeclarations();
  const int dclrs_num = (int)klass->GetNumberInstanceDeclarations();
  if(!dclrs) {
    return false;
  }

  int mem_index = 0;
  for(int i = 0; i < dclrs_num; ++i) {
    StackDclr* dclr = dclrs[i];
    if(!dclr) {
      continue;
    }

    // Compare against the trailing name without copying it.
    const std::wstring& full_name = dclr->name;
    const size_t name_index = full_name.find_last_of(L':');
    const size_t leaf_pos = (name_index == std::wstring::npos) ? 0 : name_index + 1;

    if(full_name.compare(leaf_pos, std::wstring::npos, short_name) == 0) {
      out_index = mem_index;
      return true;
    }

    mem_index++;
    if(dclr->type == FUNC_PARM) {
      mem_index++;
    }
  }

  return false;
}

// Strips the namespace and any generic arguments: "Collection.Vector<...>"
// becomes "Vector".
std::wstring DapAdapter::ClassLeafName(StackClass* klass)
{
  if(!klass) {
    return L"";
  }

  const std::wstring& name = klass->GetName();
  const std::wstring base = name.substr(0, name.find(L'<'));
  const size_t dot_index = base.find_last_of(L'.');

  return (dot_index == std::wstring::npos) ? base : base.substr(dot_index + 1);
}

// Recognizes the standard collections so they can be presented as their
// contents rather than their internals. Both the class name and the expected
// fields must match, so a user class called "Map" is not misread.
int DapAdapter::CollectionKind(StackClass* klass)
{
  if(!klass) {
    return VAR_OBJECT;
  }

  const std::wstring leaf = ClassLeafName(klass);

  // Library classes carry no declaration names, so the class name plus a
  // plausible field count is all there is to match on.
  const long fields = klass->GetNumberInstanceDeclarations();

  if((leaf == L"Vector" || leaf == L"CompareVector") && fields >= 2) {
    return VAR_VECTOR;
  }
  if(leaf == L"Map" && fields >= 3) {
    return VAR_MAP;
  }
  if(leaf == L"Hash" && fields >= 2) {
    return VAR_HASH;
  }

  return VAR_OBJECT;
}

// Element count of a recognized collection. @size sits at slot 1 in Vector and
// Hash, slot 2 in Map -- the one place that mapping is written down.
bool DapAdapter::CollectionSize(StackClass* klass, size_t* obj, long& out_size)
{
  out_size = 0;

  const int kind = CollectionKind(klass);
  if(kind == VAR_OBJECT || !obj) {
    return false;
  }

  int size_index;
  if(!FieldIndex(klass, L"@size", kind == VAR_MAP ? 2 : 1, size_index)) {
    return false;
  }

  out_size = (long)obj[size_index];
  return true;
}

// "Vector(size=3)" instead of "Collection.Vector@0x1f4a2c0". Empty when the
// class is not a recognized collection.
std::string DapAdapter::CollectionSummary(StackClass* klass, size_t* obj)
{
  long size;
  if(!CollectionSize(klass, obj, size)) {
    return "";
  }

  std::ostringstream oss;
  oss << UnicodeToBytes(ClassLeafName(klass)) << "(size=" << size << ')';
  return oss.str();
}

// Classes whose value is fully shown by DescribeObject, so drilling in would
// only expose backing storage.
bool DapAdapter::IsLeafObject(StackClass* klass)
{
  if(!klass) {
    return false;
  }

  const std::wstring& name = klass->GetName();
  return name == L"System.String" || name == L"System.IntRef" || name == L"System.BoolRef" ||
         name == L"System.CharRef" || name == L"System.ByteRef" || name == L"System.FloatRef";
}

// Renders an object as something a reader can use: the text of a string, the
// contents of a boxed scalar, an element count for a collection. Falls back to
// class@address for everything else.
std::string DapAdapter::DescribeObject(size_t* obj, StackClass* klass)
{
  if(!obj || !klass) {
    return "";
  }

  const std::wstring& class_name = klass->GetName();

  // A String keeps its Char[] in the first slot; the array stores its length
  // at [2] with the characters packed from [3].
  if(class_name == L"System.String") {
    size_t* char_array = (size_t*)obj[0];
    if(!char_array) {
      return "\"\"";
    }

    const size_t len = char_array[2];
    const wchar_t* chars = (const wchar_t*)(char_array + 3);
    std::wstring text(chars, len < 256 ? len : 256);

    std::ostringstream oss;
    oss << '"' << UnicodeToBytes(text) << '"';
    if(len > 256) {
      oss << "...";
    }
    return oss.str();
  }

  // Boxed scalars keep their payload in the first slot.
  if(class_name == L"System.IntRef") {
    std::ostringstream oss;
    oss << (long)obj[0];
    return oss.str();
  }
  if(class_name == L"System.BoolRef") {
    return obj[0] ? "true" : "false";
  }
  if(class_name == L"System.CharRef") {
    std::ostringstream oss;
    oss << '\'' << (char)(wchar_t)obj[0] << '\'';
    return oss.str();
  }
  if(class_name == L"System.ByteRef") {
    std::ostringstream oss;
    oss << (int)(unsigned char)obj[0];
    return oss.str();
  }
  if(class_name == L"System.FloatRef") {
    double value;
    memcpy(&value, &obj[0], sizeof(double));
    std::ostringstream oss;
    oss << value;
    return oss.str();
  }

  return CollectionSummary(klass, obj);
}

// Number of indexed children a value has, so a client can page through a
// large container instead of pulling it whole. 0 means "not indexed".
int DapAdapter::IndexedChildCount(ParamType type, size_t raw_value)
{
  if(raw_value == 0) {
    return 0;
  }

  switch(type) {
  case INT_ARY_PARM:
  case FLOAT_ARY_PARM:
  case OBJ_ARY_PARM: {
    const long size = (long)((size_t*)raw_value)[0];
    return size > 0 ? (int)size : 0;
  }

  case OBJ_PARM: {
    StackClass* klass = MemoryManager::GetClass((size_t*)raw_value);
    if(!klass || IsLeafObject(klass)) {
      return 0;
    }
    long size;
    if(!CollectionSize(klass, (size_t*)raw_value, size)) {
      return 0;
    }
    return size > 0 ? (int)size : 0;
  }

  default:
    return 0;
  }
}

void DapAdapter::AnnotateChildCount(json& var, ParamType type, size_t raw_value)
{
  if(!client_supports_variable_paging) {
    return;
  }

  const int count = IndexedChildCount(type, raw_value);
  if(count > 0) {
    var["indexedVariables"] = count;
  }
}

// Formats a raw slot of a known type. FormatVariableValue is declaration-driven,
// so the synthetic declaration lives here rather than at each call site.
std::string DapAdapter::FormatSlot(ParamType type, size_t* mem, int index)
{
  StackDclr dclr;
  dclr.name = L"";
  dclr.type = type;
  dclr.id = 0;

  return FormatVariableValue(dclr, mem, index);
}

// Builds one child entry, giving it its own handle when it is expandable.
json DapAdapter::MakeChildVariable(const std::string& name, ParamType type, size_t* mem, int index, int depth)
{
  StackDclr dclr;
  dclr.name = L"";
  dclr.type = type;
  dclr.id = 0;

  json var;
  var["name"] = name;
  var["value"] = FormatVariableValue(dclr, mem, index);
  var["type"] = FormatVariableType(dclr);
  var["variablesReference"] = MakeChildRef(type, mem[index], depth);
  AnnotateChildCount(var, type, mem[index]);

  return var;
}

// Decodes an array header: [0] is the element count, [1] the dimension count,
// then one size per dimension, then the elements.
bool DapAdapter::ArrayBody(size_t* array, long& out_count, size_t*& out_data)
{
  out_count = 0;
  out_data = nullptr;

  if(!array) {
    return false;
  }

  const long size = (long)array[0];
  const long dim = (long)array[1];
  if(size < 0 || dim < 1 || dim > 8) {
    return false;
  }

  out_count = size;
  out_data = array + 2 + dim;

  return true;
}

// "[7]" -- building an ostringstream per element was the bulk of the cost of
// rendering a large array.
std::string DapAdapter::ElementName(long index)
{
  return "[" + std::to_string(index) + "]";
}

void DapAdapter::ExpandObjectFields(size_t* obj_mem, StackClass* klass, int depth, json& variables)
{
  if(!obj_mem || !klass) {
    return;
  }

  StackDclr** dclrs = klass->GetInstanceDeclarations();
  const int dclrs_num = (int)klass->GetNumberInstanceDeclarations();
  if(!dclrs) {
    return;
  }

  int mem_index = 0;
  for(int i = 0; i < dclrs_num && (int)variables.size() < MAX_CHILDREN; ++i) {
    StackDclr* dclr = dclrs[i];
    if(!dclr) {
      continue;
    }

    const std::wstring full_name = dclr->name;
    const size_t name_index = full_name.find_last_of(L':');
    const std::string name = UnicodeToBytes((name_index == std::wstring::npos) ? full_name : full_name.substr(name_index + 1));

    json var;
    var["name"] = name;
    var["value"] = FormatVariableValue(*dclr, obj_mem, mem_index);
    var["type"] = FormatVariableType(*dclr);
    var["variablesReference"] = MakeChildRef(dclr->type, obj_mem[mem_index], depth);
    variables.push_back(var);

    mem_index++;
    if(dclr->type == FUNC_PARM) {
      mem_index++;
    }
  }
}

void DapAdapter::ExpandArrayElements(size_t* array, int elem_type, int depth, json& variables)
{
  long size;
  size_t* data;
  if(!ArrayBody(array, size, data)) {
    return;
  }

  ParamType element_type;
  switch((ParamType)elem_type) {
  case INT_ARY_PARM:
    element_type = INT_PARM;
    break;

  case FLOAT_ARY_PARM:
    element_type = FLOAT_PARM;
    break;

  case OBJ_ARY_PARM:
    element_type = OBJ_PARM;
    break;

  default:
    return;
  }

  const long shown = size < MAX_CHILDREN ? size : MAX_CHILDREN;

  for(long i = 0; i < shown; ++i) {
    variables.push_back(MakeChildVariable(ElementName(i), element_type, data, (int)i, depth));
  }

  if(size > shown) {
    json more;
    more["name"] = "...";
    std::ostringstream oss;
    oss << (size - shown) << " more";
    more["value"] = oss.str();
    more["variablesReference"] = 0;
    variables.push_back(more);
  }
}

void DapAdapter::ExpandVector(size_t* obj, StackClass* klass, int depth, json& variables)
{
  // Vector: @values at slot 0, @size at slot 1.
  int values_index;
  int size_index;

  if(!obj || !FieldIndex(klass, L"@values", 0, values_index) ||
     !FieldIndex(klass, L"@size", 1, size_index)) {
    ExpandObjectFields(obj, klass, depth, variables);
    return;
  }

  size_t* values = (size_t*)obj[values_index];
  const long size = (long)obj[size_index];
  if(!values || size <= 0) {
    return;
  }

  // The backing array is usually larger than the vector; only report the
  // elements actually in use.
  long capacity;
  size_t* data;
  if(!ArrayBody(values, capacity, data)) {
    return;
  }

  long shown = size < capacity ? size : capacity;
  if(shown > MAX_CHILDREN) {
    shown = MAX_CHILDREN;
  }

  for(long i = 0; i < shown; ++i) {
    variables.push_back(MakeChildVariable(ElementName(i), OBJ_PARM, data, (int)i, depth));
  }
}

// In-order walk so entries come out sorted by key, which is the order the
// Map itself iterates in.
void DapAdapter::WalkTreeNode(size_t* node, int depth, json& variables, int& count)
{
  if(!node || count >= MAX_CHILDREN || depth >= MAX_EXPANSION_DEPTH) {
    return;
  }

  StackClass* node_class = MemoryManager::GetClass(node);
  if(!node_class) {
    return;
  }

  // TreeNode: @key, @value, @left, @right, @level.
  int key_index;
  int value_index;
  int left_index;
  int right_index;

  if(!FieldIndex(node_class, L"@key", 0, key_index) ||
     !FieldIndex(node_class, L"@value", 1, value_index) ||
     !FieldIndex(node_class, L"@left", 2, left_index) ||
     !FieldIndex(node_class, L"@right", 3, right_index)) {
    return;
  }

  WalkTreeNode((size_t*)node[left_index], depth + 1, variables, count);

  if(count < MAX_CHILDREN) {
    variables.push_back(MakeChildVariable(FormatSlot(OBJ_PARM, node, key_index), OBJ_PARM, node, value_index, depth));
    count++;
  }

  WalkTreeNode((size_t*)node[right_index], depth + 1, variables, count);
}

void DapAdapter::ExpandMap(size_t* obj, StackClass* klass, int depth, json& variables)
{
  // Map: @root at slot 0, @last at 1, @size at 2.
  int root_index;

  if(!obj || !FieldIndex(klass, L"@root", 0, root_index)) {
    ExpandObjectFields(obj, klass, depth, variables);
    return;
  }

  int count = 0;
  WalkTreeNode((size_t*)obj[root_index], depth, variables, count);
}

void DapAdapter::ExpandHash(size_t* obj, StackClass* klass, int depth, json& variables)
{
  // Hash: @buckets at slot 0, @size at 1, @capacity at 2.
  int buckets_index;

  if(!obj || !FieldIndex(klass, L"@buckets", 0, buckets_index)) {
    ExpandObjectFields(obj, klass, depth, variables);
    return;
  }

  size_t* buckets = (size_t*)obj[buckets_index];
  if(!buckets) {
    return;
  }

  long bucket_count;
  size_t* bucket_data;
  if(!ArrayBody(buckets, bucket_count, bucket_data)) {
    return;
  }

  // Each occupied bucket holds a CompareList whose nodes carry a HashPair.
  for(long i = 0; i < bucket_count && (int)variables.size() < MAX_CHILDREN; ++i) {
    size_t* list = (size_t*)bucket_data[i];
    if(!list) {
      continue;
    }

    // CompareList: @size at slot 0, @head at 1.
    StackClass* list_class = MemoryManager::GetClass(list);
    int head_index;
    if(!list_class || !FieldIndex(list_class, L"@head", 1, head_index)) {
      continue;
    }

    size_t* node = (size_t*)list[head_index];
    int guard = 0;
    while(node && (int)variables.size() < MAX_CHILDREN && guard++ < MAX_CHILDREN) {
      // CompareListNode: @value at slot 0, @next at 1.
      StackClass* node_class = MemoryManager::GetClass(node);
      int node_value_index;
      int next_index;
      if(!node_class ||
         !FieldIndex(node_class, L"@value", 0, node_value_index) ||
         !FieldIndex(node_class, L"@next", 1, next_index)) {
        break;
      }

      // HashPair: @key at slot 0, @value at 1.
      size_t* pair = (size_t*)node[node_value_index];
      if(pair) {
        StackClass* pair_class = MemoryManager::GetClass(pair);
        int pair_key_index;
        int pair_value_index;
        if(pair_class &&
           FieldIndex(pair_class, L"@key", 0, pair_key_index) &&
           FieldIndex(pair_class, L"@value", 1, pair_value_index)) {
          variables.push_back(MakeChildVariable(FormatSlot(OBJ_PARM, pair, pair_key_index),
                                                OBJ_PARM, pair, pair_value_index, depth));
        }
      }

      node = (size_t*)node[next_index];
    }
  }
}

void DapAdapter::ExpandHandle(const VarHandle& handle, json& variables)
{
  const int child_depth = handle.depth + 1;

  switch(handle.kind) {
  case VAR_ARRAY:
    ExpandArrayElements(handle.ptr, handle.elem_type, child_depth, variables);
    break;

  case VAR_VECTOR:
    ExpandVector(handle.ptr, handle.klass, child_depth, variables);
    break;

  case VAR_MAP:
    ExpandMap(handle.ptr, handle.klass, child_depth, variables);
    break;

  case VAR_HASH:
    ExpandHash(handle.ptr, handle.klass, child_depth, variables);
    break;

  default:
    ExpandObjectFields(handle.ptr, handle.klass, child_depth, variables);
    break;
  }
}

void DapAdapter::HandleVariables(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  int ref = args.value("variablesReference", 0);
  json variables = json::array();

  // Drill-down into an object, array or collection captured at this stop.
  if(is_stopped && ref >= DYN_HANDLE_BASE) {
    const size_t handle_index = (size_t)(ref - DYN_HANDLE_BASE);
    if(handle_index < var_handles.size()) {
      ExpandHandle(var_handles[handle_index], variables);
    }

    // Paging: a client that asked for a window gets just that slice.
    const int start = args.value("start", 0);
    const int count = args.value("count", 0);
    if(start > 0 || count > 0) {
      json page = json::array();
      const int total = (int)variables.size();
      for(int i = start; i < total && (count <= 0 || (int)page.size() < count); ++i) {
        page.push_back(variables[i]);
      }
      variables = page;
    }

    json body;
    body["variables"] = variables;
    SendResponse(request_seq, "variables", body);
    return;
  }

  if(is_stopped && ref >= SCOPE_HANDLE_BASE && ref < VAR_HANDLE_BASE) {
    // Scope reference — enumerate locals
    int frame_index = ref - SCOPE_HANDLE_BASE;
    StackFrame* frame = GetFrameByIndex(frame_index);

    if(frame && frame->method) {
      StackDclr** dclrs = frame->method->GetDeclarations();
      int dclrs_num = frame->method->GetNumberDeclarations();

      // Offset for locals: +1 for instance methods (@self at mem[0])
      // Methods with and/or have an additional hidden variable
      int offset = 1;
      if(frame->method->HasAndOr()) {
        offset++;
      }

      // Build memory index the same way as CLI debugger's GetDeclaration:
      // FLOAT_PARM and FUNC_PARM take 2 slots
      int mem_index = 0;
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
        var["value"] = FormatVariableValue(*dclr, frame, mem_index + offset);
        var["type"] = FormatVariableType(*dclr);

        // Read the slot before advancing past it.
        const int slot = mem_index + offset;
        const long mem_slots = frame->method->GetMemorySize() + 2;
        if(slot >= 0 && slot < mem_slots && frame->mem) {
          var["variablesReference"] = MakeChildRef(dclr->type, frame->mem[slot], 0);
          AnnotateChildCount(var, dclr->type, frame->mem[slot]);
        }
        else {
          var["variablesReference"] = 0;
        }

        // Advance index: a Float is ONE slot in the 64-bit layout; only funcs take two
        mem_index++;
        if(dclr->type == FUNC_PARM) {
          mem_index++;
        }
        variables.push_back(var);
      }
    }
  }
  // Instance scope — enumerate instance variables
  else if(is_stopped && ref >= INST_SCOPE_HANDLE_BASE && ref < INST_SCOPE_HANDLE_BASE + 1000) {
    int frame_index = ref - INST_SCOPE_HANDLE_BASE;
    StackFrame* frame = GetFrameByIndex(frame_index);

    if(frame && frame->method && frame->mem && frame->mem[0]) {
      StackClass* klass = frame->method->GetClass();
      if(klass) {
        size_t* inst_mem = (size_t*)frame->mem[0];
        StackDclr** dclrs = klass->GetInstanceDeclarations();
        int dclrs_num = klass->GetNumberInstanceDeclarations();

        int mem_index = 0;
        for(int i = 0; i < dclrs_num; i++) {
          StackDclr* dclr = dclrs[i];

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
          var["value"] = FormatVariableValue(*dclr, inst_mem, mem_index);
          var["type"] = FormatVariableType(*dclr);
          var["variablesReference"] = MakeChildRef(dclr->type, inst_mem[mem_index], 0);
          variables.push_back(var);

          mem_index++;
          // A Float is ONE slot in the 64-bit layout; only funcs take two.
          if(dclr->type == FUNC_PARM) {
            mem_index++;
          }
        }
      }
    }
  }
  // Class scope — enumerate class variables
  else if(is_stopped && ref >= CLS_SCOPE_HANDLE_BASE && ref < CLS_SCOPE_HANDLE_BASE + 1000) {
    int frame_index = ref - CLS_SCOPE_HANDLE_BASE;
    StackFrame* frame = GetFrameByIndex(frame_index);

    if(frame && frame->method) {
      StackClass* klass = frame->method->GetClass();
      if(klass && klass->GetClassMemory()) {
        size_t* cls_mem = klass->GetClassMemory();
        StackDclr** dclrs = klass->GetClassDeclarations();
        int dclrs_num = klass->GetNumberClassDeclarations();

        int mem_index = 0;
        for(int i = 0; i < dclrs_num; i++) {
          StackDclr* dclr = dclrs[i];

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
          var["value"] = FormatVariableValue(*dclr, cls_mem, mem_index);
          var["type"] = FormatVariableType(*dclr);
          var["variablesReference"] = MakeChildRef(dclr->type, cls_mem[mem_index], 0);
          variables.push_back(var);

          mem_index++;
          // A Float is ONE slot in the 64-bit layout; only funcs take two.
          if(dclr->type == FUNC_PARM) {
            mem_index++;
          }
        }
      }
    }
  }

  json body;
  body["variables"] = variables;
  SendResponse(request_seq, "variables", body);
}

// Every resume clears the expansion handles: they hold raw object pointers and
// the collector moves objects, so none may outlive the stop that produced them.
void DapAdapter::Resume(bool into, bool over, bool out)
{
  std::lock_guard<std::mutex> lock(mtx);
  step_into_requested = into;
  step_over_requested = over;
  step_out_requested = out;
  resume_requested = true;
  is_stopped = false;
  ClearVarHandles();
  cv.notify_all();
}

void DapAdapter::HandleContinue(int request_seq, const json& args)
{
  SendResponse(request_seq, "continue", {{"allThreadsContinued", true}});

  Resume(false, false, false);
}

void DapAdapter::HandleNext(int request_seq, const json& args)
{
  SendResponse(request_seq, "next");

  Resume(false, true, false);
}

void DapAdapter::HandleStepIn(int request_seq, const json& args)
{
  SendResponse(request_seq, "stepIn");

  Resume(true, false, false);
}

void DapAdapter::HandleStepOut(int request_seq, const json& args)
{
  SendResponse(request_seq, "stepOut");

  Resume(false, false, true);
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
    restart_requested = false;   // a disconnect must never trigger a re-run
    resume_requested = true;
    is_stopped = false;
    cv.notify_all();
  }

  SendResponse(request_seq, "disconnect");
}

void DapAdapter::HandleEvaluate(int request_seq, const json& args)
{
  std::string expression = args.value("expression", "");
  std::string context = args.value("context", "");

  if(expression.empty() || !is_stopped) {
    SendResponse(request_seq, "evaluate", json::object(), false, "Cannot evaluate");
    return;
  }

  // Use the debugger's expression evaluator
  std::wstring wexpr = BytesToUnicode(expression);
  std::wstring resolved = wexpr;
  std::wstring result = debugger->EvaluateForDap(wexpr);

  // For hover: if lookup failed and name doesn't start with '@',
  // retry as an instance variable (editors strip the '@' prefix)
  if(result == L"<error>" && !expression.empty() && expression[0] != '@') {
    std::wstring retry = L"@" + wexpr;
    std::wstring retry_result = debugger->EvaluateForDap(retry);
    if(retry_result != L"<error>") {
      result = retry_result;
      resolved = retry;
    }
  }

  // Hand back an expandable reference when the expression resolves to an
  // object or array, so watch and hover results drill down like the
  // Variables pane rather than dead-ending on a summary line. Skip it when the
  // value could not be read at all, and resolve the same text that produced
  // the result above -- the '@' retry may have been the one that worked.
  int child_ref = 0;
  if(result != L"<error>") {
    ParamType raw_type;
    size_t raw_value;
    if(debugger->EvaluateForDapRaw(resolved, raw_type, raw_value)) {
      child_ref = MakeChildRef(raw_type, raw_value, 0);
    }
  }

  json body;
  body["result"] = UnicodeToBytes(result);
  body["variablesReference"] = child_ref;
  SendResponse(request_seq, "evaluate", body);
}

// Debug-console completion. Offers the variables visible in the selected
// frame -- locals, then instance, then class -- filtered by whatever word the
// caret sits on.
void DapAdapter::HandleCompletions(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  json targets = json::array();

  const std::string text = args.value("text", "");
  // DAP columns are 1-based and address the position after the typed text.
  int column = args.value("column", (int)text.size() + 1);
  if(column < 1) {
    column = 1;
  }
  if(column > (int)text.size() + 1) {
    column = (int)text.size() + 1;
  }

  // Walk back over the word being typed so the client can replace just it.
  int word_start = column - 1;
  while(word_start > 0) {
    const char c = text[word_start - 1];
    if(isalnum((unsigned char)c) || c == '_' || c == '@') {
      word_start--;
    }
    else {
      break;
    }
  }
  const std::string prefix = text.substr(word_start, (column - 1) - word_start);

  if(is_stopped) {
    const int frame_index = args.value("frameId", 0);
    StackFrame* frame = GetFrameByIndex(frame_index);

    std::vector<std::string> names;

    // Locals
    if(frame && frame->method) {
      StackDclr** dclrs = frame->method->GetDeclarations();
      const int dclrs_num = frame->method->GetNumberDeclarations();
      for(int i = 0; i < dclrs_num && dclrs; ++i) {
        if(dclrs[i]) {
          names.push_back(ShortDeclarationName(dclrs[i]->name));
        }
      }
    }

    // Instance and class variables of the enclosing class
    if(frame && frame->method && frame->method->GetClass()) {
      StackClass* klass = frame->method->GetClass();

      StackDclr** inst_dclrs = klass->GetInstanceDeclarations();
      const int inst_num = (int)klass->GetNumberInstanceDeclarations();
      for(int i = 0; i < inst_num && inst_dclrs; ++i) {
        if(inst_dclrs[i]) {
          names.push_back(ShortDeclarationName(inst_dclrs[i]->name));
        }
      }

      StackDclr** cls_dclrs = klass->GetClassDeclarations();
      const int cls_num = (int)klass->GetNumberClassDeclarations();
      for(int i = 0; i < cls_num && cls_dclrs; ++i) {
        if(cls_dclrs[i]) {
          names.push_back(ShortDeclarationName(cls_dclrs[i]->name));
        }
      }
    }

    for(size_t i = 0; i < names.size() && targets.size() < 100; ++i) {
      const std::string& name = names[i];
      if(name.empty()) {
        continue;
      }
      if(!prefix.empty() && name.compare(0, prefix.size(), prefix) != 0) {
        continue;
      }

      json target;
      target["label"] = name;
      target["type"] = "variable";
      target["start"] = word_start;
      target["length"] = (int)prefix.size();
      targets.push_back(target);
    }
  }

  json body;
  body["targets"] = targets;
  SendResponse(request_seq, "completions", body);
}

void DapAdapter::HandleModules(int request_seq, const json& args)
{
  json modules = json::array();

  // Objeck links everything into one executable, so the program is the only
  // module there is to report.
  if(is_launched && !program_path.empty()) {
    std::wstring name = program_path;
    const size_t sep_pos = name.find_last_of(L"/\\");
    if(sep_pos != std::wstring::npos) {
      name = name.substr(sep_pos + 1);
    }

    json module;
    module["id"] = "objeck-program";
    module["name"] = UnicodeToBytes(name);
    module["path"] = UnicodeToBytes(program_path);
    module["isOptimized"] = false;
    module["isUserCode"] = true;
    modules.push_back(module);
  }

  json body;
  body["modules"] = modules;
  body["totalModules"] = (int)modules.size();
  SendResponse(request_seq, "modules", body);
}

void DapAdapter::HandleLoadedSources(int request_seq, const json& args)
{
  json sources = json::array();

  if(debugger) {
    const std::vector<std::wstring> files = debugger->GetLoadedSourceFiles();
    for(size_t i = 0; i < files.size(); ++i) {
      json source;
      source["name"] = UnicodeToBytes(files[i]);
      source["path"] = ResolveSourcePath(files[i]);
      sources.push_back(source);
    }
  }

  json body;
  body["sources"] = sources;
  SendResponse(request_seq, "loadedSources", body);
}

// Mints the dataId a client needs before it can set a data breakpoint. The
// id is just the expression to watch, so "Break on Value Change" on a variable
// in the Variables pane becomes a watch on that name.
void DapAdapter::HandleDataBreakpointInfo(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  const std::string name = args.value("name", "");

  json body;
  if(!is_stopped || name.empty()) {
    // A null dataId tells the client this variable cannot be watched.
    body["dataId"] = nullptr;
    body["description"] = name.empty() ? "no variable selected" : "not stopped";
    SendResponse(request_seq, "dataBreakpointInfo", body);
    return;
  }

  body["dataId"] = name;
  body["description"] = name + " (breaks when the value changes)";
  // Reads are not tracked: the watch is a value comparison after each
  // instruction, which cannot see a read that leaves the value alone.
  body["accessTypes"] = json::array({"write"});
  body["canPersist"] = false;

  SendResponse(request_seq, "dataBreakpointInfo", body);
}

void DapAdapter::HandleSetDataBreakpoints(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  json breakpoints = json::array();

  if(debugger) {
    // The request carries the complete set, so replace rather than append.
    debugger->ClearDataWatches();

    const json requested = args.value("breakpoints", json::array());
    for(const auto& bp : requested) {
      const std::string data_id = bp.value("dataId", "");

      json verified;
      if(data_id.empty() || debugger->AddDataWatch(BytesToUnicode(data_id)) == 0) {
        verified["verified"] = false;
        verified["message"] = "cannot watch this expression";
      }
      else {
        verified["verified"] = true;
      }
      breakpoints.push_back(verified);
    }
  }

  json body;
  body["breakpoints"] = breakpoints;
  SendResponse(request_seq, "setDataBreakpoints", body);
}

void DapAdapter::HandleTerminate(int request_seq, const json& args)
{
  SendResponse(request_seq, "terminate");

  // Graceful counterpart to disconnect: let the client ask the program to
  // stop, then report termination as usual.
  {
    std::lock_guard<std::mutex> lock(mtx);
    disconnect_requested = true;
    resume_requested = true;
    is_stopped = false;
    ClearVarHandles();
    cv.notify_all();
  }

  SendEvent("terminated");
}

void DapAdapter::HandleBreakpointLocations(int request_seq, const json& args)
{
  json source = args.value("source", json::object());
  std::string path = source.value("path", "");

  std::string file_name = path;
  const size_t sep_pos = path.find_last_of("/\\");
  if(sep_pos != std::string::npos) {
    file_name = path.substr(sep_pos + 1);
  }

  const int start_line = args.value("line", 0);
  const int end_line = args.value("endLine", start_line);

  json locations = json::array();
  if(start_line > 0) {
    const std::wstring wfile_name = BytesToUnicode(file_name);
    // Nothing can be validated until a program is loaded -- and the debugger
    // itself does not exist before launch -- so stay permissive rather than
    // tell the editor that none of the file is breakpointable.
    const bool can_validate = is_launched && debugger;

    // One pass over the file's instructions; asking per line walked every
    // class and method in the program again for each of up to 1000 lines.
    std::set<int> executable;
    if(can_validate) {
      executable = debugger->GetExecutableLines(wfile_name);
    }

    for(int line = start_line; line <= end_line && line - start_line < 1000; ++line) {
      // Otherwise report only lines that actually carry an instruction, so
      // the client never offers a breakpoint that could not bind.
      if(!can_validate || executable.count(line)) {
        json location;
        location["line"] = line;
        locations.push_back(location);
      }
    }
  }

  json body;
  body["breakpoints"] = locations;
  SendResponse(request_seq, "breakpointLocations", body);
}

void DapAdapter::HandleExceptionInfo(int request_seq, const json& args)
{
  std::lock_guard<std::mutex> lock(mtx);

  if(!is_stopped || stop_reason != "exception") {
    SendResponse(request_seq, "exceptionInfo", json::object(), false, "Not stopped on an exception");
    return;
  }

  json body;
  body["exceptionId"] = "uncaught";
  body["breakMode"] = "unhandled";
  body["description"] = "Uncaught runtime error";

  json details;
  details["message"] = "Uncaught runtime error";
  std::ostringstream oss;
  oss << UnicodeToBytes(stopped_file) << ':' << stopped_line;
  details["stackTrace"] = oss.str();
  body["details"] = details;

  SendResponse(request_seq, "exceptionInfo", body);
}

void DapAdapter::HandleSetExpression(int request_seq, const json& args)
{
  if(!is_stopped || !debugger) {
    SendResponse(request_seq, "setExpression", json::object(), false, "Not stopped");
    return;
  }

  const std::string expression = args.value("expression", "");
  const std::string value = args.value("value", "");
  const int frame_id = args.value("frameId", 0);

  if(expression.empty()) {
    SendResponse(request_seq, "setExpression", json::object(), false, "No expression");
    return;
  }

  // setVariable works by name within a frame; an lvalue expression that is a
  // plain variable maps onto it directly.
  const std::wstring result = debugger->SetVariableForDap(frame_id, BytesToUnicode(expression), BytesToUnicode(value));
  if(result == L"<error>") {
    SendResponse(request_seq, "setExpression", json::object(), false,
                 "Cannot assign (only Int/Char/Float variables are assignable)");
    return;
  }

  json body;
  body["value"] = UnicodeToBytes(result);
  body["variablesReference"] = 0;
  SendResponse(request_seq, "setExpression", body);
}

void DapAdapter::HandleSetVariable(int request_seq, const json& args)
{
  if(!is_stopped || !debugger) {
    SendResponse(request_seq, "setVariable", json::object(), false, "Not stopped");
    return;
  }

  const int ref = args.value("variablesReference", 0);
  const std::string name = args.value("name", "");
  const std::string value = args.value("value", "");

  // map the scope handle back to a frame index
  int frame_index = -1;
  if(ref >= SCOPE_HANDLE_BASE && ref < VAR_HANDLE_BASE) {
    frame_index = ref - SCOPE_HANDLE_BASE;
  }
  else if(ref >= INST_SCOPE_HANDLE_BASE && ref < CLS_SCOPE_HANDLE_BASE) {
    frame_index = ref - INST_SCOPE_HANDLE_BASE;
  }
  else if(ref >= CLS_SCOPE_HANDLE_BASE) {
    frame_index = ref - CLS_SCOPE_HANDLE_BASE;
  }

  if(frame_index < 0 || name.empty()) {
    SendResponse(request_seq, "setVariable", json::object(), false, "Invalid variable reference");
    return;
  }

  std::wstring result = debugger->SetVariableForDap(frame_index, BytesToUnicode(name), BytesToUnicode(value));
  if(result == L"<error>") {
    SendResponse(request_seq, "setVariable", json::object(), false, "Cannot set variable (only Int/Char/Float locals are assignable)");
    return;
  }

  json body;
  body["value"] = UnicodeToBytes(result);
  body["variablesReference"] = 0;
  SendResponse(request_seq, "setVariable", body);
}

void DapAdapter::HandleSetFunctionBreakpoints(int request_seq, const json& args)
{
  json breakpoints_json = args.value("breakpoints", json::array());

  json verified = json::array();
  for(const auto& bp : breakpoints_json) {
    std::string name = bp.value("name", "");
    std::string condition_str = bp.value("condition", "");

    bool added = false;
    if(debugger && !name.empty()) {
      added = debugger->AddMethodBreak(BytesToUnicode(name), BytesToUnicode(condition_str));
    }

    json bp_result;
    bp_result["verified"] = added;
    verified.push_back(bp_result);
  }

  json body;
  body["breakpoints"] = verified;
  SendResponse(request_seq, "setFunctionBreakpoints", body);
}

void DapAdapter::HandleSetExceptionBreakpoints(int request_seq, const json& args)
{
  // enable break-on-error if the "uncaught" filter is selected
  break_on_exception = false;
  json filters = args.value("filters", json::array());
  for(const auto& f : filters) {
    if(f.is_string() && f.get<std::string>() == "uncaught") {
      break_on_exception = true;
    }
  }

  SendResponse(request_seq, "setExceptionBreakpoints");
}

void DapAdapter::HandleRestart(int request_seq, const json& args)
{
  // Ask the VM worker thread to end the current run and loop back into DapRun.
  // We set restart_requested (NOT disconnect_requested) so the parked run halts
  // via RequestHalt while the adapter's main loop keeps serving requests.
  // Breakpoints persist in the Debugger across runs.
  {
    std::lock_guard<std::mutex> lock(mtx);
    restart_requested = true;
    resume_requested = true;
    is_stopped = false;
    is_terminated = false;
    cv.notify_all();
  }

  SendResponse(request_seq, "restart");
}

void DapAdapter::EmitLogPoint(const std::wstring& message)
{
  // interpolate {expr} fragments by evaluating each in the current frame
  std::wstring out;
  size_t i = 0;
  while(i < message.size()) {
    if(message[i] == L'{') {
      const size_t end = message.find(L'}', i);
      if(end != std::wstring::npos) {
        const std::wstring expr = message.substr(i + 1, end - i - 1);
        std::wstring value = debugger ? debugger->EvaluateForDap(expr) : L"<error>";
        out += value;
        i = end + 1;
        continue;
      }
    }
    out += message[i++];
  }
  out += L"\n";

  json body;
  body["category"] = "console";
  body["output"] = UnicodeToBytes(out);
  SendEvent("output", body);
}

// ============================================
// Callbacks from Debugger
// ============================================

void DapAdapter::OnStopped(const std::string& reason, int line, const std::wstring& file,
                           StackFrame* frame, StackFrame** call_stack, long call_stack_pos)
{
  // While draining the current run for a disconnect/restart, don't surface
  // breakpoint stops to the client.
  if(disconnect_requested) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mtx);
    is_stopped = true;
    resume_requested = false;
    step_into_requested = false;
    step_over_requested = false;
    step_out_requested = false;
    // Handles from the previous stop describe memory that may have moved.
    ClearVarHandles();
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

  // Name the watch that fired and what it changed from, otherwise the client
  // just says "paused" with no indication of which value moved.
  if(reason == "data breakpoint" && debugger) {
    const std::string text = UnicodeToBytes(debugger->GetLastWatchText());
    const std::string old_value = UnicodeToBytes(debugger->GetLastWatchOld());
    const std::string new_value = UnicodeToBytes(debugger->GetLastWatchNew());
    body["description"] = text + " changed";
    body["text"] = text + ": " + old_value + " -> " + new_value;
  }

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
// Frame Lookup
// ============================================

StackFrame* DapAdapter::GetFrameByIndex(int frame_index)
{
  if(frame_index == (int)stopped_call_stack_pos) {
    return stopped_frame;
  }
  else if(frame_index >= 0 && frame_index < (int)stopped_call_stack_pos) {
    return stopped_call_stack[frame_index];
  }
  return nullptr;
}

// ============================================
// Variable Formatting
// ============================================

std::string DapAdapter::FormatVariableValue(StackDclr& dclr, StackFrame* frame, int var_index)
{
  if(!frame || !frame->mem || var_index < 0) {
    return "<unavailable>";
  }

  // Bounds: mem_size is a slot count (NewMemory allocates mem_size + 2 size_t
  // slots; +2 covers the @self slot and the and/or temporary).
  long mem_slots = frame->method->GetMemorySize() + 2;
  if(var_index >= mem_slots) {
    return "<out of scope>";
  }

  return FormatVariableValue(dclr, frame->mem, var_index);
}

std::string DapAdapter::FormatVariableValue(StackDclr& dclr, size_t* mem, int var_index)
{
  if(!mem || var_index < 0) {
    return "<unavailable>";
  }

  size_t value = mem[var_index];

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
      memcpy(&dval, &mem[var_index], sizeof(double));
      std::ostringstream oss;
      oss << dval;
      return oss.str();
    }

    case CHAR_ARY_PARM: {
      if(value == 0) {
        return "Nil";
      }
      size_t* array = (size_t*)value;
      const size_t len = array[2];
      const wchar_t* chars = (const wchar_t*)(array + 3);
      if(len > 0) {
        const size_t preview_max = 48;
        std::wstring wstr(chars, std::min(len, preview_max));
        std::string result = "\"" + UnicodeToBytes(wstr) + "\"";
        if(len > preview_max) {
          result += "...";
        }
        return result;
      }
      return "\"\"";
    }

    case BYTE_ARY_PARM:
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
      StackClass* klass = MemoryManager::GetClass((size_t*)value);
      std::ostringstream oss;
      if(klass) {
        // Strings, boxed scalars and collections describe themselves; an
        // address is what a reader actually wants least.
        const std::string described = DescribeObject((size_t*)value, klass);
        if(!described.empty()) {
          return described;
        }
        oss << UnicodeToBytes(klass->GetName()) << "@0x" << std::hex << value;
      }
      else {
        oss << "object@0x" << std::hex << value;
      }
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
