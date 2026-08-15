/***************************************************************************
 * Spawns and drains a child process for the editor's run pane (editor
 * phase 4).
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_CHILD_RUN_H__
#define __TUI_CHILD_RUN_H__

#include "../shared/sys.h"   // UnicodeToBytes: the locale-independent UTF-8 encoder
#include <string>
#include <vector>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#endif

// The in-process VM has main-thread affinity (process-global Loader/
// MemoryManager/StackInterpreter plus its own GC threads), so a program run
// on a worker thread deadlocks even trivial code. F5 therefore compiles and
// runs the buffer as a real child process instead: its stdout and stderr are
// merged into a pipe the editor drains without blocking, so the UI stays live,
// Esc can kill a runaway loop, and the child gets its own console/JIT that obi
// itself (built _NO_JIT) lacks.
//
// The program path and arguments come from a user buffer, so the child is
// spawned from an explicit argument vector with NO shell (mirroring obu's
// RunArgv): nothing in argv is ever reinterpreted, which is the whole defense
// against a filename or argument carrying shell metacharacters.

namespace Tui {

  // The C locale must not get a say when narrowing argv/env for exec -- and
  // shared/sys.h::UnicodeToBytes is exactly the locale-independent encoder
  // added for that reason, so it is used rather than re-derived here.

  class Child {
    bool running;
    int exit_code;
#ifdef _WIN32
    HANDLE proc;
    HANDLE read_pipe;   // our (non-inheritable) read end of the child's output
#else
    pid_t pid;
    int read_fd;        // our non-blocking read end of the child's output
#endif

#ifdef _WIN32
    // CreateProcess wants one command line, not an argv, so each argument is
    // requoted per the CRT's parsing rules (backslashes only matter before a
    // quote). Without this a temp path with a space would split into two args.
    static std::wstring QuoteArg(const std::wstring& arg) {
      if(!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
      }
      std::wstring out = L"\"";
      for(size_t i = 0; i < arg.size(); ++i) {
        size_t slashes = 0;
        while(i < arg.size() && arg[i] == L'\\') {
          ++slashes;
          ++i;
        }
        if(i == arg.size()) {
          out.append(slashes * 2, L'\\');   // trailing: double so the closing " stays literal
          break;
        }
        if(arg[i] == L'"') {
          out.append(slashes * 2 + 1, L'\\');
          out += L'"';
        }
        else {
          out.append(slashes, L'\\');
          out += arg[i];
        }
      }
      out += L'"';
      return out;
    }

    // Build a fresh environment block from the parent's, overriding (or adding)
    // the requested names. Done as a copy rather than SetEnvironmentVariable so
    // obi's own environment -- which its in-process paths also read -- is left
    // untouched.
    static std::vector<wchar_t> BuildEnvBlock(const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {
      std::vector<wchar_t> block;
      const wchar_t* parent = GetEnvironmentStringsW();
      if(parent) {
        for(const wchar_t* p = parent; *p; ) {
          const std::wstring entry(p);
          p += entry.size() + 1;
          const size_t eq = entry.find(L'=');
          const std::wstring name = (eq == std::wstring::npos) ? entry : entry.substr(0, eq);
          bool overridden = false;
          for(const auto& over : overrides) {
            if(_wcsicmp(name.c_str(), over.first.c_str()) == 0) {
              overridden = true;
              break;
            }
          }
          // keep everything not overridden -- including the legacy "=X:" drive
          // current-directory entries, whose name is empty and must survive
          if(!overridden) {
            block.insert(block.end(), entry.begin(), entry.end());
            block.push_back(L'\0');
          }
        }
        FreeEnvironmentStringsW(const_cast<wchar_t*>(parent));
      }
      for(const auto& over : overrides) {
        const std::wstring entry = over.first + L'=' + over.second;
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
      }
      block.push_back(L'\0');   // block terminator
      return block;
    }
#endif

  public:
    Child() : running(false), exit_code(-1) {
#ifdef _WIN32
      proc = INVALID_HANDLE_VALUE;
      read_pipe = INVALID_HANDLE_VALUE;
#else
      pid = -1;
      read_fd = -1;
#endif
    }

    ~Child() {
      Close();
    }

    // non-copyable: it owns OS handles
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    bool IsRunning() {
      if(!running) {
        return false;
      }
#ifdef _WIN32
      if(WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(proc, &code);
        exit_code = (int)code;
        running = false;
      }
#else
      int status = 0;
      const pid_t got = waitpid(pid, &status, WNOHANG);
      if(got == pid) {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        running = false;
      }
      else if(got < 0) {
        running = false;
      }
#endif
      return running;
    }

    int ExitCode() const {
      return exit_code;
    }

    // Append whatever output is waiting, without blocking. Returns false only
    // when the pipe is gone; an empty read is a normal "nothing yet".
    bool Drain(std::string& out) {
#ifdef _WIN32
      if(read_pipe == INVALID_HANDLE_VALUE) {
        return false;
      }
      for(;;) {
        DWORD avail = 0;
        if(!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
          return false;   // child closed the write end
        }
        if(avail == 0) {
          break;
        }
        char buffer[4096];
        const DWORD want = avail < sizeof(buffer) ? avail : (DWORD)sizeof(buffer);
        DWORD got = 0;
        if(!ReadFile(read_pipe, buffer, want, &got, nullptr) || got == 0) {
          return false;
        }
        out.append(buffer, got);
      }
      return true;
#else
      if(read_fd < 0) {
        return false;
      }
      char buffer[4096];
      for(;;) {
        const ssize_t got = read(read_fd, buffer, sizeof(buffer));
        if(got > 0) {
          out.append(buffer, (size_t)got);
          continue;
        }
        if(got == 0) {
          break;   // EOF: child closed the write end
        }
        if(errno == EINTR) {
          continue;
        }
        break;     // EAGAIN/EWOULDBLOCK: nothing more for now
      }
      return true;
#endif
    }

    // SIGTERM (then SIGKILL) on POSIX, TerminateProcess on Windows; reaps so no
    // zombie is left behind. Used when the user presses Esc on a running loop.
    void Kill() {
      if(!running) {
        return;
      }
#ifdef _WIN32
      TerminateProcess(proc, 1);
      WaitForSingleObject(proc, INFINITE);
      DWORD code = 0;
      GetExitCodeProcess(proc, &code);
      exit_code = (int)code;
      running = false;
#else
      kill(pid, SIGTERM);
      for(int i = 0; i < 50; ++i) {
        int status = 0;
        if(waitpid(pid, &status, WNOHANG) == pid) {
          exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
          running = false;
          return;
        }
        usleep(2000);   // 2ms; a well-behaved child dies on SIGTERM at once
      }
      kill(pid, SIGKILL);
      int status = 0;
      waitpid(pid, &status, 0);
      exit_code = 128;
      running = false;
#endif
    }

    // Launches argv[0] with the given arguments and environment overrides. The
    // child's stdin is the null device so a program that reads input sees EOF
    // rather than stealing the editor's key bytes.
    bool Start(const std::vector<std::wstring>& argv,
               const std::vector<std::pair<std::wstring, std::wstring>>& env) {
      if(argv.empty()) {
        return false;
      }
#ifdef _WIN32
      SECURITY_ATTRIBUTES sa;
      sa.nLength = sizeof(sa);
      sa.bInheritHandle = TRUE;
      sa.lpSecurityDescriptor = nullptr;

      HANDLE write_pipe = INVALID_HANDLE_VALUE;
      if(!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        read_pipe = INVALID_HANDLE_VALUE;
        return false;
      }
      // the child must not inherit our read end, or the pipe never reports EOF
      SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

      HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, 0, nullptr);

      STARTUPINFOW si;
      ZeroMemory(&si, sizeof(si));
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = write_pipe;
      si.hStdError = write_pipe;
      si.hStdInput = nul;

      std::wstring command;
      for(size_t i = 0; i < argv.size(); ++i) {
        if(i) {
          command += L' ';
        }
        command += QuoteArg(argv[i]);
      }
      std::vector<wchar_t> cmd_buffer(command.begin(), command.end());
      cmd_buffer.push_back(L'\0');

      std::vector<wchar_t> env_block = BuildEnvBlock(env);

      PROCESS_INFORMATION pi;
      ZeroMemory(&pi, sizeof(pi));
      const BOOL ok = CreateProcessW(nullptr, cmd_buffer.data(), nullptr, nullptr, TRUE,
                                     CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                                     env_block.data(), nullptr, &si, &pi);
      // our copies of the child-side handles are done regardless of outcome
      if(write_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(write_pipe);
      }
      if(nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
      }
      if(!ok) {
        CloseHandle(read_pipe);
        read_pipe = INVALID_HANDLE_VALUE;
        return false;
      }
      CloseHandle(pi.hThread);
      proc = pi.hProcess;
      running = true;
      return true;
#else
      int pipe_fds[2];
      if(pipe(pipe_fds) != 0) {
        return false;
      }

      // build the child-side argv (UTF-8) before forking; after fork only
      // async-signal-safe work is allowed
      std::vector<std::string> narrow;
      narrow.reserve(argv.size());
      for(const std::wstring& a : argv) {
        narrow.push_back(UnicodeToBytes(a));
      }
      std::vector<char*> c_argv;
      c_argv.reserve(narrow.size() + 1);
      for(std::string& a : narrow) {
        c_argv.push_back(&a[0]);
      }
      c_argv.push_back(nullptr);

      std::vector<std::pair<std::string, std::string>> narrow_env;
      narrow_env.reserve(env.size());
      for(const auto& e : env) {
        narrow_env.push_back({ UnicodeToBytes(e.first), UnicodeToBytes(e.second) });
      }

      pid = fork();
      if(pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
      }
      if(pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        const int null_in = open("/dev/null", O_RDONLY);
        if(null_in >= 0) {
          dup2(null_in, STDIN_FILENO);
          close(null_in);
        }
        for(const auto& e : narrow_env) {
          setenv(e.first.c_str(), e.second.c_str(), 1);
        }
        execvp(c_argv[0], c_argv.data());
        _exit(127);
      }

      close(pipe_fds[1]);
      read_fd = pipe_fds[0];
      const int flags = fcntl(read_fd, F_GETFL, 0);
      fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
      running = true;
      return true;
#endif
    }

  private:
    void Close() {
      if(running) {
        Kill();
      }
#ifdef _WIN32
      if(read_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(read_pipe);
        read_pipe = INVALID_HANDLE_VALUE;
      }
      if(proc != INVALID_HANDLE_VALUE) {
        CloseHandle(proc);
        proc = INVALID_HANDLE_VALUE;
      }
#else
      if(read_fd >= 0) {
        close(read_fd);
        read_fd = -1;
      }
#endif
    }
  };
}

#endif
