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
#include "obj_layout.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using json = nlohmann::json;

namespace Runtime {

  // Variable reference handle encoding:
  // Local scope handles: 1000 + frame_index
  // Variable handles: 2000 + (frame_index * 1000) + var_index
  // Instance scope handles: 3000 + frame_index
  // Class scope handles: 4000 + frame_index
  static const int SCOPE_HANDLE_BASE = 1000;
  static const int VAR_HANDLE_BASE = 2000;
  static const int INST_SCOPE_HANDLE_BASE = 3000;
  static const int CLS_SCOPE_HANDLE_BASE = 4000;

  // Dynamic handles for drilling into objects, arrays and collections.
  // Allocated on demand while stopped and discarded on every resume: they hold
  // raw object pointers and the collector moves objects, so a handle must never
  // outlive the stop that produced it.
  static const int DYN_HANDLE_BASE = 100000;
  static const int MAX_EXPANSION_DEPTH = 12;
  static const int MAX_CHILDREN = 256;
  static const size_t MAX_VAR_HANDLES = 20000;

  struct VarHandle {
    int kind;
    size_t* ptr;
    StackClass* klass;
    int elem_type;
    int depth;
  };

  class DapAdapter {
    // Debugger integration
    Debugger* debugger;

    // DAP protocol state
    // Outgoing sequence number. Written only by SendMessage, under send_mtx.
    int seq;
    bool is_initialized;
    bool is_launched;
    bool is_terminated;

    // Threading synchronization
    std::mutex mtx;
    std::condition_variable cv;
    // Written on the DAP main thread under mtx, but read on the VM execution
    // thread without it -- Debugger::ProcessInstruction polls the Is* accessors at
    // every instruction, and several handlers read is_stopped directly. As plain
    // bools that was a data race, and the practical failure is worse than tearing:
    // nothing stopped this -O3 -flto build from hoisting the load out of the
    // dispatch loop, so a disconnect or step request could go unseen indefinitely.
    // Atomics are seq_cst by default, which is all these flags need. The existing
    // mtx usage is unchanged and still guards multi-field transitions and the cv
    // predicates -- atomics fix the race on each flag, not check-then-act ordering.
    std::atomic<bool> is_stopped;
    std::atomic<bool> resume_requested;
    std::atomic<bool> step_into_requested;
    std::atomic<bool> step_over_requested;
    std::atomic<bool> step_out_requested;
    std::atomic<bool> disconnect_requested;
    std::atomic<bool> restart_requested;
    // Same cross-thread pattern as the flags above -- written by
    // HandleSetExceptionBreakpoints on the DAP thread, read by the VM thread via
    // BreaksOnException(). Coverity does not report this one because its writes
    // never hold mtx, so the "written under lock elsewhere" heuristic never fires;
    // it is nonetheless the least protected of the set.
    std::atomic<bool> break_on_exception;

    // Declared by the client on the initialize request.
    bool client_supports_variable_paging;
    bool client_supports_progress;
    bool client_supports_invalidated;

    // Requests are read by a thread of their own (see Inbox in dap.cpp) so that a
    // 'cancel' is noticed while an earlier request is still being answered. The
    // main thread asks it whether the request it is working on was cancelled.
  public:
    struct Inbox;
  private:
    std::shared_ptr<Inbox> inbox;
    int current_request_seq;
    // the startup progress report is ended once, by whoever gets there first
    std::atomic<bool> startup_progress_open;
    bool Cancelled();
    // test hook: OBJECK_DAP_TEST_DELAY_MS holds evaluate and variables that long
    // (or until they are cancelled), standing in for a request that is slow
    bool DelayForTest();
    void SendProgressStart(const std::string& id, const std::string& title, const std::string& message, int request_seq);
    void SendProgressUpdate(const std::string& id, const std::string& message);
    void SendProgressEnd(const std::string& id, const std::string& message);
    // tells a client that supports it to re-read what it showed of the stopped program
    void SendInvalidated();

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

    // OS-level fds for the JSON-RPC transport. We dup() the real
    // stdin/stdout before redirecting process stdout/stderr to capture
    // pipes (so the user program's PrintLine doesn't corrupt the DAP
    // protocol). All DAP reads/writes go through these fds, not via
    // std::cin / std::cout.
    int dap_in_fd;
    int dap_out_fd;

    // Pipes that capture the running program's stdout/stderr; reader
    // threads forward the bytes as DAP `output` events.
    int prog_stdout_pipe[2];
    int prog_stderr_pipe[2];
    std::thread stdout_reader_thread;
    std::thread stderr_reader_thread;
    // how many reader threads still run, so the end of a run can wait for
    // the program's last output to go out before `terminated` does
    std::mutex reader_mtx;
    std::condition_variable reader_cv;
    int readers_running = 0;

    // The VM execution thread spawned in HandleConfigurationDone. Stored
    // as a member (not detached) so Run() can join it on disconnect and
    // avoid the VM racing the process teardown (which would otherwise
    // cause access violations as the C runtime tears down state the VM
    // is still using).
    std::thread vm_thread;
    // True while the VM worker thread is inside its run loop. The halt watchdog
    // reads it to tell a program that unwound from one that is stuck.
    std::atomic<bool> vm_running{false};

    // Serializes SendMessage across the main DAP thread and the two
    // output reader threads (otherwise their headers/bodies interleave
    // on dap_out_fd and the client sees corrupt frames).
    std::mutex send_mtx;

    void RedirectProgramStdio();
    void StartOutputReaders();
    void DrainProgramOutput();
    // After disconnect / terminate: halt every VM thread, and if the worker has
    // not unwound within a couple of seconds (a thread blocked in a syscall
    // cannot see a halt), flush what was printed and end the process.
    void HaltProgramAndWatch();
    void OutputReaderLoop(int fd, const std::string& category);

    // JSON-RPC transport
    std::string ReadMessage();
    // Takes the message by reference: it stamps the outgoing seq under send_mtx.
    void SendMessage(json& msg);
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
    void Resume(bool into, bool over, bool out);
    void HandleContinue(int seq, const json& args);
    void HandleNext(int seq, const json& args);
    void HandleStepIn(int seq, const json& args);
    void HandleStepOut(int seq, const json& args);
    void HandleStepInTargets(int seq, const json& args);
    void HandleGotoTargets(int seq, const json& args);
    void HandleGoto(int seq, const json& args);
    void HandleCancel(int seq, const json& args);
    void HandleDisassemble(int seq, const json& args);

    // A disassembly address names one instruction of one method: the class id,
    // the method id and the instruction's index packed into one number, so that
    // consecutive instructions have consecutive addresses (a client that does
    // arithmetic on them, or sorts them, gets the right answer).
    static std::string EncodeAddress(long cls_id, long mthd_id, long ip);
    static bool DecodeAddress(const std::string& text, long& cls_id, long& mthd_id, long& ip);
    std::string DescribeInstruction(StackMethod* method, long ip);
    void HandlePause(int seq, const json& args);
    void HandleDisconnect(int seq, const json& args);
    void HandleEvaluate(int seq, const json& args);
    void HandleSetVariable(int seq, const json& args);
    void HandleSetFunctionBreakpoints(int seq, const json& args);
    void HandleSetExceptionBreakpoints(int seq, const json& args);
    void HandleRestart(int seq, const json& args);
    void HandleTerminate(int seq, const json& args);
    void HandleBreakpointLocations(int seq, const json& args);
    void HandleExceptionInfo(int seq, const json& args);
    void HandleSetExpression(int seq, const json& args);
    void HandleCompletions(int seq, const json& args);
    void HandleModules(int seq, const json& args);
    void HandleLoadedSources(int seq, const json& args);
    void HandleDataBreakpointInfo(int seq, const json& args);
    void HandleSetDataBreakpoints(int seq, const json& args);

    // Source path resolution
    std::string ResolveSourcePath(const std::wstring& file_name, const std::wstring& class_name = L"");

    // Frame lookup helper
    StackFrame* GetFrameByIndex(int frame_index);

    // Variable formatting helpers
    std::string FormatVariableType(StackDclr& dclr);

    // Variable drill-down. Handles live only for the duration of one stop.
    std::vector<VarHandle> var_handles;

    void ClearVarHandles();
    int AllocVarHandle(int kind, size_t* ptr, StackClass* klass, int elem_type, int depth);
    int MakeChildRef(ParamType type, size_t raw_value, int depth);
    std::string ShortDeclarationName(const std::wstring& full_name);
    json MakeChildVariable(const std::string& name, ParamType type, size_t* mem, int index, int depth);
    int IndexedChildCount(ParamType type, size_t raw_value);
    void AnnotateChildCount(json& var, ParamType type, size_t raw_value);
    void ExpandHandle(const VarHandle& handle, json& variables);
    void ExpandObjectFields(size_t* obj_mem, StackClass* klass, int depth, json& variables);
    void ExpandArrayElements(size_t* array, int elem_type, int depth, json& variables);
    void ExpandVector(size_t* obj, StackClass* klass, int depth, json& variables);
    void ExpandMap(size_t* obj, StackClass* klass, int depth, json& variables);
    void ExpandHash(size_t* obj, StackClass* klass, int depth, json& variables);
    void ExpandPrimitiveVector(size_t* obj, StackClass* klass, int depth, json& variables);
    void ExpandIntTable(size_t* obj, StackClass* klass, int depth, bool has_values, json& variables);
    void ExpandBitSet(size_t* obj, StackClass* klass, int depth, json& variables);
    void WalkTreeNode(size_t* node, int depth, json& variables, int& count);

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

    // Called by the debugger once the program's image has been loaded, which is
    // the end of the 'starting the program' phase a client shows progress for
    void OnProgramLoaded();

    // Called from Debugger::ProcessInstruction for a logpoint: emit the message
    // (with {expr} interpolation) as an output event without stopping
    void EmitLogPoint(const std::wstring& message);

    // Check if DAP wants a specific stepping mode
    bool IsStepInto() const { return step_into_requested; }
    bool IsStepOver() const { return step_over_requested; }
    bool IsStepOut() const { return step_out_requested; }
    bool IsDisconnected() const { return disconnect_requested; }

    // True between a restart request and the next run starting; the current run
    // must halt (but the adapter must keep running, unlike a disconnect)
    bool IsRestarting() const { return restart_requested; }

    // True when the client enabled the "uncaught" exception breakpoint filter
    bool BreaksOnException() const { return break_on_exception; }
  };
}

#endif
