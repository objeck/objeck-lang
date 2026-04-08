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

  // Start the program on a background thread. Store the thread as a
  // member so Run() can join it on shutdown — detaching would let the
  // VM race the C runtime teardown and access-violate.
  if(debugger && is_launched) {
    vm_thread = std::thread([this]() {
      debugger->DapRun();
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

        // Advance index: floats and funcs take 2 slots
        mem_index++;
        if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
          mem_index++;
        }
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
  if(!frame || !frame->mem || var_index < 0) {
    return "<unavailable>";
  }

  // Check bounds: mem_size is in bytes, each slot is sizeof(size_t)
  long mem_slots = frame->method->GetMemorySize() / sizeof(size_t);
  if(var_index >= mem_slots) {
    return "<out of scope>";
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
      // Validate pointer before dereferencing
      size_t* array = (size_t*)value;
      if(IsBadReadPtr(array, sizeof(size_t))) {
        return "Nil";
      }
      std::ostringstream oss;
      oss << "[size=" << array[0] << "]";
      return oss.str();
    }

    case OBJ_PARM: {
      if(value == 0) {
        return "Nil";
      }
      if(IsBadReadPtr((void*)value, sizeof(size_t))) {
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
