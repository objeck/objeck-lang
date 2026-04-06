/***************************************************************************
* Debug Adapter Protocol (DAP) support for the Objeck debugger
*
* Copyright (c) 2026, Randy Hollines
* All rights reserved.
*
* See LICENSE file for full copyright notice.
***************************************************************************/

#ifndef __DAP_H__
#define __DAP_H__

#include "json.hpp"
#include "debugger.h"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <sstream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using json = nlohmann::json;

namespace Runtime {

  // Variable reference handle encoding:
  // Scope handles: 1000 + frame_index
  // Variable handles: 2000 + (frame_index * 1000) + var_index
  static const int SCOPE_HANDLE_BASE = 1000;
  static const int VAR_HANDLE_BASE = 2000;

  class DapAdapter {
    // Debugger integration
    Debugger* debugger;

    // DAP protocol state
    int seq;
    bool is_initialized;
    bool is_launched;
    bool is_terminated;

    // Threading synchronization
    std::mutex mtx;
    std::condition_variable cv;
    bool is_stopped;
    bool resume_requested;
    bool step_into_requested;
    bool step_over_requested;
    bool step_out_requested;
    bool disconnect_requested;

    // Captured state at breakpoint
    int stopped_line;
    std::wstring stopped_file;
    std::string stop_reason;
    StackFrame* stopped_frame;
    StackFrame** stopped_call_stack;
    long stopped_call_stack_pos;

    // Program parameters
    std::wstring program_path;
    std::wstring source_dir;
    std::wstring program_args;

    // JSON-RPC transport
    std::string ReadMessage();
    void SendMessage(const json& msg);
    void SendResponse(int request_seq, const std::string& command, const json& body = json::object(), bool success = true, const std::string& message = "");
    void SendEvent(const std::string& event, const json& body = json::object());

    // DAP request handlers
    void HandleInitialize(int seq, const json& args);
    void HandleLaunch(int seq, const json& args);
    void HandleSetBreakpoints(int seq, const json& args);
    void HandleConfigurationDone(int seq, const json& args);
    void HandleThreads(int seq);
    void HandleStackTrace(int seq, const json& args);
    void HandleScopes(int seq, const json& args);
    void HandleVariables(int seq, const json& args);
    void HandleContinue(int seq, const json& args);
    void HandleNext(int seq, const json& args);
    void HandleStepIn(int seq, const json& args);
    void HandleStepOut(int seq, const json& args);
    void HandlePause(int seq, const json& args);
    void HandleDisconnect(int seq, const json& args);
    void HandleEvaluate(int seq, const json& args);

    // Source path resolution
    std::string ResolveSourcePath(const std::wstring& file_name);

    // Variable formatting helpers
    std::string FormatVariableValue(StackDclr& dclr, StackFrame* frame, int var_index);
    std::string FormatVariableType(StackDclr& dclr);

  public:
    DapAdapter();
    ~DapAdapter();

    // Main DAP message loop (runs on main thread)
    void Run();

    // Called from Debugger::ProcessInstruction when in DAP mode
    void OnStopped(const std::string& reason, int line, const std::wstring& file,
                   StackFrame* frame, StackFrame** call_stack, long call_stack_pos);

    // Called when program terminates
    void OnTerminated();

    // Check if DAP wants a specific stepping mode
    bool IsStepInto() const { return step_into_requested; }
    bool IsStepOver() const { return step_over_requested; }
    bool IsStepOut() const { return step_out_requested; }
    bool IsDisconnected() const { return disconnect_requested; }
  };
}

#endif
