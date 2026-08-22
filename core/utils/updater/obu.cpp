/***************************************************************************
* Objeck updater ("obu")
*
* Phase 1: 'obu check' compares the installed version against a GitHub
* release tag. Exit codes: 0 = update available, 1 = up to date, 2 = error.
*
* Copyright (c) 2026, Randy Hollines
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

#include "../../shared/version.h"
#include "../../shared/exe_path.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#define EXIT_UPDATE_AVAILABLE 0
#define EXIT_UP_TO_DATE 1
#define EXIT_CHECK_ERROR 2

#define RELEASES_API_BASE "https://api.github.com/repos/objeck/objeck-lang/releases"

// The release asset for this platform (see release-build.yml). Windows update
// is intentionally not implemented in this phase -- its in-place swap needs the
// copy-self-and-re-exec dance (phase 3), so obu reports rather than pretends.
#if defined(_WIN32)
#define OBU_ASSET_PREFIX ""
#define OBU_UPDATE_SUPPORTED 0
#elif defined(__APPLE__)
#define OBU_ASSET_PREFIX "objeck-macos-arm64"
#define OBU_UPDATE_SUPPORTED 1
#elif defined(__linux__)
#if defined(__aarch64__)
#define OBU_ASSET_PREFIX "objeck-linux-arm64"
#else
#define OBU_ASSET_PREFIX "objeck-linux-x64"
#endif
#define OBU_UPDATE_SUPPORTED 1
#else
#define OBU_ASSET_PREFIX ""
#define OBU_UPDATE_SUPPORTED 0
#endif

/****************************
* Usage
****************************/
static void Usage()
{
  std::cout << "Usage: obu <command> [options]" << std::endl << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "  check              check whether a newer Objeck release is available" << std::endl;
  std::cout << "  update             download, verify and install the latest release in place" << std::endl;
  std::cout << "  rollback           restore the version kept by the last successful update" << std::endl << std::endl;
  std::cout << "Options for 'check' and 'update':" << std::endl;
  std::cout << "  --quiet            print nothing; communicate via the exit code" << std::endl;
  std::cout << "  --channel <tag>    target a specific release tag (e.g. v2026.8.0)" << std::endl;
  std::cout << "                     instead of the latest release" << std::endl;
  std::cout << "  --force            (update) install even if the tree is already current" << std::endl << std::endl;
  std::cout << "General options:" << std::endl;
  std::cout << "  --help             show this message" << std::endl;
  std::cout << "  --version          show the obu version" << std::endl << std::endl;
  std::cout << "Exit codes: 0 = action taken / update available, 1 = up to date, 2 = error" << std::endl;
}

/****************************
* Converts the compiled-in wide
* version string to narrow text
****************************/
static std::string InstalledVersion()
{
  const wchar_t* wide_version = VERSION_STRING;
  std::string version;
  for(size_t i = 0; wide_version[i] != L'\0'; ++i) {
    version += static_cast<char>(wide_version[i]);
  }

  return version;
}

/****************************
* Restricts a release tag to an allowlist before it is spliced into a request
* URL. No shell is involved any more (see FetchUrl), so this is now about
* keeping '--channel' from reshaping the URL path rather than a command line.
****************************/
static bool IsSafeTag(const std::string& tag)
{
  if(tag.empty()) {
    return false;
  }

  for(size_t i = 0; i < tag.size(); ++i) {
    const char c = tag[i];
    if(!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
      return false;
    }
  }

  return true;
}

// Reported when the child could not be launched at all, as opposed to running
// and failing -- the caller needs to tell "curl is not installed" from "curl
// answered 404". 127 is the shell convention for it, and is what a failed
// execvp already yields here; curl's own codes stop at 99, so there is no
// collision in practice.
#define OBU_SPAWN_FAILED 127

#ifdef _WIN32
/****************************
* Quotes one argument per the CommandLineToArgvW rules. CreateProcess takes a
* single string rather than a vector, but no shell parses it -- cmd.exe is
* never involved -- so metacharacters are inert regardless. This only keeps an
* argument from being split on whitespace or losing its quotes in transit.
****************************/
static std::string QuoteWindowsArg(const std::string& arg)
{
  if(!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
    return arg;
  }

  std::string quoted = "\"";
  for(size_t i = 0; ; ++i) {
    size_t slashes = 0;
    while(i < arg.size() && arg[i] == '\\') {
      ++i;
      ++slashes;
    }

    if(i == arg.size()) {
      // double the trailing run so it cannot escape the closing quote
      quoted.append(slashes * 2, '\\');
      break;
    }

    // a run of backslashes is only special immediately before a quote
    quoted.append(arg[i] == '"' ? slashes * 2 + 1 : slashes, '\\');
    quoted += arg[i];
  }

  return quoted + '"';
}

/****************************
* Runs a program with an explicit argument vector and captures its stdout.
* CreateProcess with a null lpApplicationName searches PATH but never spawns a
* shell, so nothing in args is interpreted. Returns true only if the child
* launched and exited 0; *exit_code gets the status, or OBU_SPAWN_FAILED.
****************************/
static bool RunArgvCapture(const std::vector<std::string>& args, std::string& out,
                           bool quiet = false, int* exit_code = nullptr)
{
  out.clear();
  if(exit_code) { *exit_code = OBU_SPAWN_FAILED; }
  if(args.empty()) {
    return false;
  }

  SECURITY_ATTRIBUTES attrs;
  attrs.nLength = sizeof(attrs);
  attrs.lpSecurityDescriptor = nullptr;
  attrs.bInheritHandle = TRUE;

  HANDLE read_end = nullptr, write_end = nullptr;
  if(!CreatePipe(&read_end, &write_end, &attrs, 0)) {
    return false;
  }
  // the child must not inherit our read end or the read below never sees EOF
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  HANDLE null_out = INVALID_HANDLE_VALUE;
  if(quiet) {
    null_out = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                           &attrs, OPEN_EXISTING, 0, nullptr);
  }

  std::string command_line;
  for(size_t i = 0; i < args.size(); ++i) {
    if(i > 0) { command_line += ' '; }
    command_line += QuoteWindowsArg(args[i]);
  }
  std::vector<char> writable(command_line.begin(), command_line.end());
  writable.push_back('\0');

  STARTUPINFOA start;
  ZeroMemory(&start, sizeof(start));
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  start.hStdOutput = write_end;
  start.hStdError = null_out != INVALID_HANDLE_VALUE ? null_out : GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION proc;
  ZeroMemory(&proc, sizeof(proc));

  const BOOL launched = CreateProcessA(nullptr, writable.data(), nullptr, nullptr,
                                       TRUE, 0, nullptr, nullptr, &start, &proc);
  // drop our copies of the child's ends so the pipe can reach EOF
  CloseHandle(write_end);
  if(null_out != INVALID_HANDLE_VALUE) { CloseHandle(null_out); }

  if(!launched) {
    CloseHandle(read_end);
    return false;
  }

  char buffer[4096];
  DWORD got = 0;
  while(ReadFile(read_end, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
    out.append(buffer, got);
  }
  CloseHandle(read_end);

  WaitForSingleObject(proc.hProcess, INFINITE);
  DWORD status = 0;
  if(!GetExitCodeProcess(proc.hProcess, &status)) {
    status = static_cast<DWORD>(OBU_SPAWN_FAILED);
  }
  CloseHandle(proc.hProcess);
  CloseHandle(proc.hThread);

  if(exit_code) { *exit_code = static_cast<int>(status); }

  return status == 0;
}
#else
/****************************
* Runs a program with an explicit argument vector and captures its stdout.
* execvp takes the vector directly, so no shell exists to interpret anything
* in it. Returns true only if the child launched and exited 0; *exit_code gets
* the status, or OBU_SPAWN_FAILED.
****************************/
static bool RunArgvCapture(const std::vector<std::string>& args, std::string& out,
                           bool quiet = false, int* exit_code = nullptr)
{
  out.clear();
  if(exit_code) { *exit_code = OBU_SPAWN_FAILED; }
  if(args.empty()) {
    return false;
  }

  int pipe_fds[2];
  if(pipe(pipe_fds) != 0) {
    return false;
  }

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for(const std::string& a : args) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if(pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return false;
  }
  if(pid == 0) {
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    if(quiet) {
      const int devnull = open("/dev/null", O_WRONLY);
      if(devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
    }
    execvp(argv[0], argv.data());
    _exit(OBU_SPAWN_FAILED);
  }

  close(pipe_fds[1]);
  char buffer[4096];
  ssize_t got;
  while((got = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
    out.append(buffer, static_cast<size_t>(got));
  }
  close(pipe_fds[0]);

  int status = 0;
  while(waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    /* retry */
  }
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : OBU_SPAWN_FAILED;
  if(exit_code) { *exit_code = code; }

  return code == 0;
}
#endif

/****************************
* Fetches a URL by running the system 'curl' binary directly -- argv form, so
* the URL is passed as an argument and no shell ever sees it. It used to be
* interpolated into a popen() command string, which made a URL carrying shell
* metacharacters a command-injection vector (CWE-78) reachable from the
* '--channel' argument and from a release's own asset URLs.
****************************/
static bool FetchUrl(const std::string& url, bool is_quiet, std::string& response, std::string& error)
{
  int exit_code = 0;
  if(!RunArgvCapture({"curl", "-fsSL", "--max-time", "20", url}, response, is_quiet, &exit_code)) {
    if(exit_code == OBU_SPAWN_FAILED) {
      error = "Unable to run 'curl'; please ensure it is installed and on the path.";
    }
    else {
      error = "Request failed: " + url + " ('curl' exited with code " +
        std::to_string(exit_code) + "; is curl installed and the network reachable?)";
    }
    return false;
  }

  return true;
}

/****************************
* Extracts "tag_name" from a
* GitHub release JSON response
****************************/
static bool ExtractTagName(const std::string& json, std::string& tag)
{
  const std::string key = "\"tag_name\"";
  size_t index = json.find(key);
  if(index == std::string::npos) {
    return false;
  }
  index += key.size();

  index = json.find(':', index);
  if(index == std::string::npos) {
    return false;
  }

  index = json.find('"', index);
  if(index == std::string::npos) {
    return false;
  }
  ++index;

  const size_t end = json.find('"', index);
  if(end == std::string::npos || end == index) {
    return false;
  }

  tag = json.substr(index, end - index);
  return true;
}

/****************************
* Parses a version tag such as
* 'v2026.8.0' into numeric parts
****************************/
static bool ParseVersion(const std::string& tag, std::vector<long>& parts)
{
  parts.clear();

  size_t index = 0;
  if(index < tag.size() && (tag[index] == 'v' || tag[index] == 'V')) {
    ++index;
  }

  if(index >= tag.size()) {
    return false;
  }

  std::string component;
  for(; index <= tag.size(); ++index) {
    if(index == tag.size() || tag[index] == '.') {
      if(component.empty()) {
        return false;
      }
      parts.push_back(std::strtol(component.c_str(), nullptr, 10));
      component.clear();
    }
    else if(std::isdigit(static_cast<unsigned char>(tag[index]))) {
      component += tag[index];
    }
    else {
      return false;
    }
  }

  return !parts.empty();
}

/****************************
* Compares versions numerically;
* returns <0, 0 or >0
****************************/
static int CompareVersions(const std::vector<long>& left, const std::vector<long>& right)
{
  const size_t count = left.size() > right.size() ? left.size() : right.size();
  for(size_t i = 0; i < count; ++i) {
    const long left_part = i < left.size() ? left[i] : 0;
    const long right_part = i < right.size() ? right[i] : 0;
    if(left_part != right_part) {
      return left_part < right_part ? -1 : 1;
    }
  }

  return 0;
}

/****************************
* 'check' command
****************************/
static int DoCheck(bool is_quiet, const std::string& channel)
{
  const std::string installed = InstalledVersion();

  std::string url = RELEASES_API_BASE;
  if(channel.empty()) {
    url += "/latest";
  }
  else {
    if(!IsSafeTag(channel)) {
      if(!is_quiet) {
        std::cerr << "Invalid channel tag: '" << channel << '\'' << std::endl;
      }
      return EXIT_CHECK_ERROR;
    }
    url += "/tags/" + channel;
  }

  std::string response, error;
  if(!FetchUrl(url, is_quiet, response, error)) {
    if(!is_quiet) {
      std::cerr << error << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  std::string release_tag;
  if(!ExtractTagName(response, release_tag)) {
    if(!is_quiet) {
      std::cerr << "Unable to find \"tag_name\" in the release response from " << url << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  std::vector<long> installed_parts, release_parts;
  if(!ParseVersion(installed, installed_parts)) {
    if(!is_quiet) {
      std::cerr << "Malformed installed version: '" << installed << '\'' << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  if(!ParseVersion(release_tag, release_parts)) {
    if(!is_quiet) {
      std::cerr << "Malformed release tag: '" << release_tag << '\'' << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  const bool update_available = CompareVersions(installed_parts, release_parts) < 0;
  if(!is_quiet) {
    std::cout << "Installed version: " << installed << std::endl;
    std::cout << (channel.empty() ? "Latest release: " : "Channel release: ") << release_tag << std::endl;
    if(update_available) {
      std::cout << "An update is available." << std::endl;
    }
    else {
      std::cout << "Objeck is up to date." << std::endl;
    }
  }

  return update_available ? EXIT_UPDATE_AVAILABLE : EXIT_UP_TO_DATE;
}

/* ===================================================================
 * Update engine (POSIX). Windows is deferred to phase 3.
 * =================================================================== */

/****************************
* SHA-256, implemented inline so the integrity gate depends on nothing
* external -- an updater whose hash check can be shimmed by a missing or
* replaced system tool is not an integrity gate at all. Public-domain
* construction (FIPS 180-4).
****************************/
namespace sha256 {
  struct Ctx {
    uint32_t state[8];
    uint64_t bits;
    uint8_t buffer[64];
    size_t buffer_len;
  };

  static inline uint32_t Ror(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }

  static void Init(Ctx& c) {
    static const uint32_t iv[8] = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::memcpy(c.state, iv, sizeof(iv));
    c.bits = 0;
    c.buffer_len = 0;
  }

  static void Block(Ctx& c, const uint8_t* p) {
    static const uint32_t k[64] = {
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
    uint32_t w[64];
    for(int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
             (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
    }
    for(int i = 16; i < 64; ++i) {
      const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c.state[0], b = c.state[1], cc = c.state[2], d = c.state[3];
    uint32_t e = c.state[4], f = c.state[5], g = c.state[6], h = c.state[7];
    for(int i = 0; i < 64; ++i) {
      const uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = h + S1 + ch + k[i] + w[i];
      const uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
      const uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
      const uint32_t t2 = S0 + maj;
      h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c.state[0] += a; c.state[1] += b; c.state[2] += cc; c.state[3] += d;
    c.state[4] += e; c.state[5] += f; c.state[6] += g; c.state[7] += h;
  }

  static void Update(Ctx& c, const uint8_t* data, size_t len) {
    c.bits += uint64_t(len) * 8;
    while(len > 0) {
      const size_t take = (64 - c.buffer_len < len) ? 64 - c.buffer_len : len;
      std::memcpy(c.buffer + c.buffer_len, data, take);
      c.buffer_len += take;
      data += take;
      len -= take;
      if(c.buffer_len == 64) {
        Block(c, c.buffer);
        c.buffer_len = 0;
      }
    }
  }

  static std::string Final(Ctx& c) {
    const uint64_t bits = c.bits;
    uint8_t pad = 0x80;
    Update(c, &pad, 1);
    pad = 0;
    while(c.buffer_len != 56) {
      Update(c, &pad, 1);
    }
    uint8_t len_be[8];
    for(int i = 0; i < 8; ++i) {
      len_be[i] = uint8_t(bits >> (56 - i * 8));
    }
    // Update() would re-count these length bytes; write the final block directly
    std::memcpy(c.buffer + 56, len_be, 8);
    Block(c, c.buffer);

    static const char* hex = "0123456789abcdef";
    std::string out;
    for(int i = 0; i < 8; ++i) {
      for(int b = 3; b >= 0; --b) {
        const uint8_t byte = uint8_t(c.state[i] >> (b * 8));
        out += hex[byte >> 4];
        out += hex[byte & 0xF];
      }
    }
    return out;
  }
}

/****************************
* Lowercase hex SHA-256 of a file, or empty on read failure
****************************/
static std::string Sha256File(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  if(!in) {
    return "";
  }
  sha256::Ctx c;
  sha256::Init(c);
  char buffer[65536];
  while(in) {
    in.read(buffer, sizeof(buffer));
    const std::streamsize got = in.gcount();
    if(got > 0) {
      sha256::Update(c, reinterpret_cast<const uint8_t*>(buffer), static_cast<size_t>(got));
    }
  }
  return sha256::Final(c);
}

// ExecutableDir() comes from shared/exe_path.h -- obi's F5 needs the same
// resolution to find obc/obr, and two copies of the MAX_PATH/dyld/proc
// handling was one too many.

// obu lives in <root>/bin; the install root is one level up. An override is
// honored only for the offline test harness (see the fake-release test), never
// as ordinary configuration.
static fs::path InstallRoot()
{
#ifdef OBU_TEST_HOOKS
  if(const char* override_root = std::getenv("OBU_INSTALL_ROOT")) {
    return fs::path(override_root);
  }
#endif
  const fs::path bin = ExecutableDir();
  return bin.empty() ? fs::path() : bin.parent_path();
}

#if OBU_UPDATE_SUPPORTED
/****************************
* Runs a program with an explicit argument vector -- NO shell. Nothing in
* args is ever interpreted, so an asset name or install path carrying shell
* metacharacters (`$(...)`, backticks, quotes) is inert. This is the whole
* defense against command injection from an attacker-controlled release, and
* it is why obu never builds a command string for curl/tar/obr.
* Returns the child exit code, or 127 if it could not be launched.
****************************/
static int RunArgv(const std::vector<std::string>& args, bool quiet)
{
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for(const std::string& a : args) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if(pid < 0) {
    return 127;
  }
  if(pid == 0) {
    if(quiet) {
      const int devnull = open("/dev/null", O_WRONLY);
      if(devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
    }
    execvp(argv[0], argv.data());
    _exit(127);
  }

  int status = 0;
  while(waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    /* retry */
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : OBU_SPAWN_FAILED;
}
#endif

/****************************
* A release asset name must be a plain filename over a strict allowlist. This
* is defense-in-depth on top of the no-shell argv execution: the name is used
* to match a SHA256SUMS line and in messages, never in a command line.
****************************/
static bool IsSafeAssetName(const std::string& name)
{
  if(name.empty() || name.size() > 128 || name.front() == '.') {
    return false;
  }
  for(const char c : name) {
    if(!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
      return false;
    }
  }
  return name.find("..") == std::string::npos;
}

/****************************
* Fetches a release JSON document, honoring OBU_RELEASE_JSON_FILE for the
* offline test harness so the whole flow runs in CI without the network.
****************************/
static bool FetchReleaseJson(const std::string& channel, bool is_quiet, std::string& json, std::string& error)
{
#ifdef OBU_TEST_HOOKS
  if(const char* json_file = std::getenv("OBU_RELEASE_JSON_FILE")) {
    std::ifstream in(json_file, std::ios::binary);
    if(!in) {
      error = std::string("Unable to read OBU_RELEASE_JSON_FILE: ") + json_file;
      return false;
    }
    json.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
  }
#endif

  std::string url = RELEASES_API_BASE;
  if(channel.empty()) {
    url += "/latest";
  }
  else {
    if(!IsSafeTag(channel)) {
      error = "Invalid channel tag: '" + channel + '\'';
      return false;
    }
    url += "/tags/" + channel;
  }
  return FetchUrl(url, is_quiet, json, error);
}

/****************************
* Extracts the browser_download_url of the asset whose name starts with
* 'prefix' and ends with 'suffix'. The name is validated before any use.
****************************/
static bool ExtractAssetUrl(const std::string& json, const std::string& prefix,
                            const std::string& suffix, std::string& out_url, std::string& out_name)
{
  size_t search = 0;
  const std::string name_key = "\"name\"";
  while((search = json.find(name_key, search)) != std::string::npos) {
    size_t i = json.find(':', search + name_key.size());
    if(i == std::string::npos) { break; }
    i = json.find('"', i);
    if(i == std::string::npos) { break; }
    ++i;
    const size_t name_end = json.find('"', i);
    if(name_end == std::string::npos) { break; }
    const std::string name = json.substr(i, name_end - i);
    search = name_end + 1;

    const bool prefix_ok = prefix.empty() || name.compare(0, prefix.size(), prefix) == 0;
    const bool suffix_ok = suffix.empty() ? (name == prefix || prefix.empty() ? true : false)
                                          : (name.size() >= suffix.size() &&
                                             name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
    // for a suffix-less lookup (SHA256SUMS) require an exact name match
    const bool match = suffix.empty() ? (name == prefix) : (prefix_ok && suffix_ok);
    if(!match) {
      continue;
    }

    // the asset name must be a plain filename; reject anything with path
    // separators or traversal before it is ever written to disk
    if(name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
       name.find("..") != std::string::npos) {
      continue;
    }

    const size_t url_key = json.find("\"browser_download_url\"", name_end);
    if(url_key == std::string::npos) { continue; }
    size_t u = json.find(':', url_key);
    u = json.find('"', u);
    if(u == std::string::npos) { continue; }
    ++u;
    const size_t url_end = json.find('"', u);
    if(url_end == std::string::npos) { continue; }
    const std::string url = json.substr(u, url_end - u);

    // only accept an https URL on a github host; the download goes to a shell
    if(url.compare(0, 8, "https://") != 0) { continue; }
    if(url.find("://github.com/") == std::string::npos &&
       url.find(".githubusercontent.com/") == std::string::npos) {
      continue;
    }
    if(url.find('"') != std::string::npos || url.find('`') != std::string::npos ||
       url.find('$') != std::string::npos) {
      continue;
    }

    out_url = url;
    out_name = name;
    return true;
  }
  return false;
}

/****************************
* Downloads a URL to a file (curl -o), or copies from OBU_ASSET_DIR/<name> when
* the offline test harness supplies local assets.
****************************/
static bool DownloadAsset(const std::string& url, const std::string& name,
                          const fs::path& dest, bool is_quiet, std::string& error)
{
#ifdef OBU_TEST_HOOKS
  if(const char* asset_dir = std::getenv("OBU_ASSET_DIR")) {
    std::error_code ec;
    fs::copy_file(fs::path(asset_dir) / name, dest, fs::copy_options::overwrite_existing, ec);
    if(ec) {
      error = "Unable to stage local asset '" + name + "': " + ec.message();
      return false;
    }
    return true;
  }
#endif
  (void)name;   // used only by the offline copy path above

#if OBU_UPDATE_SUPPORTED
  // argv, not a command string: the url and dest are passed literally to curl
  if(RunArgv({"curl", "-fsSL", "--max-time", "300", "-o", dest.string(), url}, is_quiet) != 0) {
    error = "Download failed: " + url;
    return false;
  }
  return true;
#else
  (void)url; (void)dest; (void)is_quiet;
  error = "Download is not supported on this platform";
  return false;
#endif
}

/****************************
* Looks up the expected hash for 'name' in a SHA256SUMS document
* ('<hex>  <name>' per line)
****************************/
static bool ExpectedHash(const std::string& sums, const std::string& name, std::string& hash)
{
  size_t pos = 0;
  while(pos < sums.size()) {
    size_t eol = sums.find('\n', pos);
    if(eol == std::string::npos) { eol = sums.size(); }
    std::string line = sums.substr(pos, eol - pos);
    pos = eol + 1;

    if(!line.empty() && line.back() == '\r') { line.pop_back(); }   // tolerate CRLF
    const size_t sp = line.find(' ');
    if(sp == std::string::npos || sp != 64) { continue; }
    const std::string file = line.substr(line.find_last_of(' ') + 1);
    if(file == name || file == "*" + name) {
      hash = line.substr(0, 64);
      for(char& c : hash) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
      return true;
    }
  }
  return false;
}

// Top-level install entries obu manages; dot-directories obu creates are never
// touched by the swap.
static bool IsObuWorkDir(const std::string& name)
{
  return name == ".obu-work" || name == ".previous" || name == ".obu-rollback";
}

/****************************
* Moves every managed entry from 'from' into 'to' (created if needed).
* Returns false and stops on the first failure so the caller can unwind.
****************************/
static bool MoveTreeEntries(const fs::path& from, const fs::path& to, std::string& error)
{
  std::error_code ec;
  // snapshot the entries first: renaming out of a directory while iterating it
  // is unspecified, and the throwing operator++ could otherwise abort mid-swap
  std::vector<fs::path> entries;
  for(fs::directory_iterator it(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    entries.push_back(it->path());
  }
  if(ec) {
    error = "Unable to read '" + from.string() + "': " + ec.message();
    return false;
  }

  fs::create_directories(to, ec);
  for(const fs::path& path : entries) {
    const std::string name = path.filename().string();
    if(IsObuWorkDir(name)) {
      continue;
    }
    fs::rename(path, to / name, ec);
    if(ec) {
      error = "Unable to move '" + name + "': " + ec.message();
      return false;
    }
  }
  return true;
}

// Removes every managed entry directly (used to clear partial content during
// an unwind). Snapshots first, ignores per-entry errors -- best effort.
static void RemoveManagedEntries(const fs::path& root)
{
  std::error_code ec;
  std::vector<fs::path> entries;
  for(fs::directory_iterator it(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    entries.push_back(it->path());
  }
  for(const fs::path& path : entries) {
    if(!IsObuWorkDir(path.filename().string())) {
      fs::remove_all(path, ec);
    }
  }
}

/****************************
* The staging tree may be the extracted root itself or a single top-level
* directory inside it; the payload is whichever contains bin/.
****************************/
static fs::path DetectPayloadRoot(const fs::path& staging)
{
  std::error_code ec;
  if(fs::exists(staging / "bin", ec)) {
    return staging;
  }
  fs::path only;
  int dirs = 0;
  for(fs::directory_iterator it(staging, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if(it->is_directory(ec)) {
      only = it->path();
      ++dirs;
    }
  }
  if(dirs == 1 && fs::exists(only / "bin", ec)) {
    return only;
  }
  return fs::path();
}

#if OBU_UPDATE_SUPPORTED
/****************************
* An exclusive lock held for the whole of update/rollback, so two obu
* processes cannot corrupt each other's staging and backup directories.
* Returns the held fd (>= 0) or -1 if another obu holds it.
****************************/
static int AcquireLock(const fs::path& root)
{
  const std::string lock_path = (root / ".obu.lock").string();
  const int fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
  if(fd < 0) {
    return -1;
  }
  if(flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/****************************
* If a previous run died mid-swap -- current tree archived to .previous but the
* new payload not yet installed -- root is left without bin/. Detect that and
* complete the interrupted rollback so the install is never left unusable.
****************************/
static bool RecoverInterruptedSwap(const fs::path& root, bool is_quiet)
{
  std::error_code ec;
  const fs::path previous = root / ".previous";
  if(fs::exists(root / "bin", ec) || !fs::exists(previous / "bin", ec)) {
    return false;
  }
  if(!is_quiet) {
    std::cout << "A previous update was interrupted; restoring the last version." << std::endl;
  }
  std::string error;
  MoveTreeEntries(previous, root, error);
  fs::remove_all(previous, ec);
  fs::remove_all(root / ".obu-work", ec);
  return true;
}
#endif

/****************************
* 'update' command
****************************/
static int DoUpdate(bool is_quiet, const std::string& channel, bool force)
{
#if !OBU_UPDATE_SUPPORTED
  (void)is_quiet; (void)channel; (void)force;
  std::cerr << "In-place update is not yet available on this platform; "
               "please reinstall from https://www.objeck.org." << std::endl;
  return EXIT_CHECK_ERROR;
#else
  const fs::path root = InstallRoot();
  std::error_code ec;
  if(root.empty()) {
    std::cerr << "Could not locate the Objeck install directory." << std::endl;
    return EXIT_CHECK_ERROR;
  }

  const int lock_fd = AcquireLock(root);
  if(lock_fd < 0) {
    std::cerr << "Another obu operation is in progress (or the install root is not writable)." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  RecoverInterruptedSwap(root, is_quiet);
  if(!fs::exists(root / "bin", ec)) {
    std::cerr << "Could not locate the Objeck install root (expected a bin/ beside obu)." << std::endl;
    close(lock_fd);
    return EXIT_CHECK_ERROR;
  }

  const fs::path work = root / ".obu-work";
  const fs::path previous = root / ".previous";
  const fs::path staging = work / "staging";

  // A single cleanup+unlock path so no early return leaks staging or the lock.
  struct Guard {
    const fs::path& work; int fd; bool armed = true;
    ~Guard() { if(armed) { std::error_code e; fs::remove_all(work, e); } if(fd >= 0) { close(fd); } }
  } guard{work, lock_fd};

  // resolve the target release
  std::string json, error;
  if(!FetchReleaseJson(channel, is_quiet, json, error)) {
    std::cerr << error << std::endl;
    return EXIT_CHECK_ERROR;
  }
  std::string release_tag;
  if(!ExtractTagName(json, release_tag)) {
    std::cerr << "Unable to find a release tag in the response." << std::endl;
    return EXIT_CHECK_ERROR;
  }

  // A release tag that will not parse is an error, never a silent proceed.
  std::vector<long> installed_parts, release_parts;
  if(!ParseVersion(release_tag, release_parts)) {
    std::cerr << "Release tag '" << release_tag << "' is not a version obu can compare." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  if(ParseVersion(InstalledVersion(), installed_parts) && !force) {
    const int cmp = CompareVersions(installed_parts, release_parts);
    if(cmp == 0) {
      if(!is_quiet) {
        std::cout << "Objeck is already at " << release_tag << "; nothing to do." << std::endl;
      }
      return EXIT_UP_TO_DATE;
    }
    if(cmp > 0) {
      // an explicit older --channel is a downgrade; require --force so it is a
      // deliberate act, and say so rather than claiming "already at"
      std::cerr << "Release " << release_tag << " is older than the installed "
                << InstalledVersion() << "; pass --force to downgrade." << std::endl;
      return EXIT_UP_TO_DATE;
    }
  }

  // locate the platform asset and its checksums
  std::string asset_url, asset_name, sums_url, sums_name;
  if(!ExtractAssetUrl(json, OBU_ASSET_PREFIX, ".tgz", asset_url, asset_name)) {
    std::cerr << "Release " << release_tag << " has no " OBU_ASSET_PREFIX " asset "
                 "(a macOS notarization gap does this -- try again once it publishes)." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  if(!IsSafeAssetName(asset_name)) {
    std::cerr << "Refusing an asset with an unexpected name: '" << asset_name << "'." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  if(!ExtractAssetUrl(json, "SHA256SUMS", "", sums_url, sums_name)) {
    std::cerr << "Release " << release_tag << " has no SHA256SUMS asset; refusing to update "
                 "without an integrity manifest." << std::endl;
    return EXIT_CHECK_ERROR;
  }

  fs::remove_all(work, ec);
  fs::create_directories(work, ec);
  if(ec) {
    std::cerr << "Unable to create the staging directory: " << ec.message() << std::endl;
    return EXIT_CHECK_ERROR;
  }

  // Download to FIXED local names -- the remote asset name is never used as a
  // path, only to look up its line in SHA256SUMS.
  const fs::path asset_path = work / "asset.tgz";
  const fs::path sums_path = work / "SHA256SUMS";
  if(!DownloadAsset(asset_url, asset_name, asset_path, is_quiet, error) ||
     !DownloadAsset(sums_url, sums_name, sums_path, is_quiet, error)) {
    std::cerr << error << std::endl;
    return EXIT_CHECK_ERROR;
  }

  // VERIFY BEFORE ANYTHING IS TOUCHED. This gate proves the download was not
  // corrupted in transit; it does NOT prove the release was published by a
  // trusted party (SHA256SUMS ships on the same channel as the asset). The
  // anti-substitution layer -- Authenticode/notarization checks -- is a
  // separate control tracked in UPDATER_DESIGN.md, not yet enforced here.
  std::ifstream sums_in(sums_path, std::ios::binary);
  std::string sums((std::istreambuf_iterator<char>(sums_in)), std::istreambuf_iterator<char>());
  std::string expected;
  if(!ExpectedHash(sums, asset_name, expected)) {
    std::cerr << "SHA256SUMS does not list " << asset_name << "; refusing to update." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  const std::string actual = Sha256File(asset_path);
  if(actual.empty() || actual != expected) {
    std::cerr << "Integrity check FAILED for " << asset_name << " (expected " << expected
              << ", got " << actual << "). Nothing was changed." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  if(!is_quiet) {
    std::cout << "Verified " << asset_name << " against SHA256SUMS." << std::endl;
  }

  // reject a tarball with an absolute or traversing member before extracting
  std::string listing;
  if(!RunArgvCapture({"tar", "-tzf", asset_path.string()}, listing)) {
    std::cerr << "Unable to read the archive " << asset_name << "." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  for(size_t p = 0; p < listing.size();) {
    size_t eol = listing.find('\n', p);
    if(eol == std::string::npos) { eol = listing.size(); }
    const std::string member = listing.substr(p, eol - p);
    p = eol + 1;
    if(!member.empty() && (member.front() == '/' || member.compare(0, 3, "../") == 0 ||
                           member.find("/../") != std::string::npos)) {
      std::cerr << "Archive contains an unsafe path ('" << member << "'); refusing." << std::endl;
      return EXIT_CHECK_ERROR;
    }
  }

  // extract into a fresh staging dir (never into the live tree)
  fs::create_directories(staging, ec);
  if(RunArgv({"tar", "--no-same-owner", "-xzf", asset_path.string(), "-C", staging.string()}, is_quiet) != 0) {
    std::cerr << "Unable to unpack " << asset_name << "." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  const fs::path payload = DetectPayloadRoot(staging);
  if(payload.empty()) {
    std::cerr << "The downloaded archive did not contain an Objeck tree (no bin/)." << std::endl;
    return EXIT_CHECK_ERROR;
  }

  // all-or-nothing swap: current tree -> .previous, staging payload -> root
  fs::remove_all(previous, ec);
  if(!MoveTreeEntries(root, previous, error)) {
    std::cerr << "Swap failed while archiving the current version: " << error
              << ". Attempting to restore." << std::endl;
    if(!MoveTreeEntries(previous, root, error)) {
      std::cerr << "RESTORE ALSO FAILED. Your install is split between the root and "
                << previous << "; move the contents of that directory back by hand." << std::endl;
      guard.armed = false;   // leave staging in place for diagnosis
      return EXIT_CHECK_ERROR;
    }
    fs::remove_all(previous, ec);
    return EXIT_CHECK_ERROR;
  }
  if(!MoveTreeEntries(payload, root, error)) {
    std::cerr << "Swap failed while installing the new version: " << error << ". Rolling back." << std::endl;
    RemoveManagedEntries(root);
    MoveTreeEntries(previous, root, error);
    fs::remove_all(previous, ec);
    return EXIT_CHECK_ERROR;
  }

  // POST-CHECK: the design names 'obc -v' as authoritative for what is
  // installed. On failure, roll back automatically. ('obr' has no version
  // flag, so it must not be used here.)
  const int health = RunArgv({(root / "bin" / "obc").string(), "-v"}, is_quiet);
  if(health != 0) {
    std::cerr << "The updated Objeck failed its post-install check; rolling back." << std::endl;
    RemoveManagedEntries(root);
    MoveTreeEntries(previous, root, error);
    fs::remove_all(previous, ec);
    return EXIT_CHECK_ERROR;
  }

  // success: staging is cleaned by the guard; .previous is kept for rollback
  if(!is_quiet) {
    std::cout << "Updated to " << release_tag << ". The previous version is kept for 'obu rollback'." << std::endl;
  }
  return EXIT_UPDATE_AVAILABLE;
#endif
}

/****************************
* 'rollback' command -- restores the version kept by the last successful update
****************************/
static int DoRollback(bool is_quiet)
{
#if !OBU_UPDATE_SUPPORTED
  (void)is_quiet;
  std::cerr << "Rollback is not available on this platform." << std::endl;
  return EXIT_CHECK_ERROR;
#else
  const fs::path root = InstallRoot();
  std::error_code ec;
  if(root.empty()) {
    std::cerr << "Could not locate the Objeck install directory." << std::endl;
    return EXIT_CHECK_ERROR;
  }

  const int lock_fd = AcquireLock(root);
  if(lock_fd < 0) {
    std::cerr << "Another obu operation is in progress (or the install root is not writable)." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  RecoverInterruptedSwap(root, is_quiet);

  const fs::path previous = root / ".previous";
  // require a REAL saved tree, not merely a leftover directory -- otherwise a
  // rollback would move the live install aside and restore nothing
  if(!fs::exists(previous / "bin", ec)) {
    std::cerr << "There is no previous version to roll back to." << std::endl;
    close(lock_fd);
    return EXIT_CHECK_ERROR;
  }

  // current -> holding, previous -> root, then discard holding
  const fs::path holding = root / ".obu-rollback";
  fs::remove_all(holding, ec);
  std::string error;
  if(!MoveTreeEntries(root, holding, error)) {
    std::cerr << "Rollback failed while setting aside the current version: " << error << std::endl;
    MoveTreeEntries(holding, root, error);
    close(lock_fd);
    return EXIT_CHECK_ERROR;
  }
  if(!MoveTreeEntries(previous, root, error)) {
    std::cerr << "Rollback failed while restoring: " << error << ". Attempting to undo." << std::endl;
    RemoveManagedEntries(root);
    MoveTreeEntries(holding, root, error);
    close(lock_fd);
    return EXIT_CHECK_ERROR;
  }
  fs::remove_all(holding, ec);   // the version we rolled back FROM is discarded
  fs::remove_all(previous, ec);

  const int health = RunArgv({(root / "bin" / "obc").string(), "-v"}, is_quiet);
  close(lock_fd);
  if(health != 0) {
    std::cerr << "Warning: the restored version did not pass its health check." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  if(!is_quiet) {
    std::cout << "Rolled back to the previous version." << std::endl;
  }
  return EXIT_UPDATE_AVAILABLE;
#endif
}

/****************************
* Program start
****************************/
int main(int argc, const char* argv[])
{
  if(argc < 2) {
    Usage();
    return EXIT_CHECK_ERROR;
  }

  const std::string command = argv[1];
  if(command == "--help" || command == "-h" || command == "help") {
    Usage();
    return 0;
  }

  if(command == "--version" || command == "version") {
    std::cout << "obu " << InstalledVersion() << std::endl;
    return 0;
  }

  if(command == "rollback") {
    bool is_quiet = false;
    for(int i = 2; i < argc; ++i) {
      const std::string option = argv[i];
      if(option == "--quiet" || option == "-q") {
        is_quiet = true;
      }
      else {
        std::cerr << "Unknown option for 'rollback': '" << option << '\'' << std::endl;
        return EXIT_CHECK_ERROR;
      }
    }
    return DoRollback(is_quiet);
  }

  if(command != "check" && command != "update") {
    std::cerr << "Unknown command: '" << command << '\'' << std::endl << std::endl;
    Usage();
    return EXIT_CHECK_ERROR;
  }

  bool is_quiet = false;
  bool force = false;
  std::string channel;
  for(int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    if(option == "--quiet" || option == "-q") {
      is_quiet = true;
    }
    else if(option == "--force" && command == "update") {
      force = true;
    }
    else if(option == "--channel") {
      if(i + 1 >= argc) {
        std::cerr << "Option '--channel' requires a release tag argument" << std::endl;
        return EXIT_CHECK_ERROR;
      }
      channel = argv[++i];
    }
    else {
      std::cerr << "Unknown option: '" << option << '\'' << std::endl << std::endl;
      Usage();
      return EXIT_CHECK_ERROR;
    }
  }

  return command == "update" ? DoUpdate(is_quiet, channel, force) : DoCheck(is_quiet, channel);
}
