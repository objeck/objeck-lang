/***************************************************************************
* Objeck updater ("obu")
*
* 'obu check' compares the installed version against a GitHub release tag.
* 'obu update' verifies and installs it in place; 'obu rollback' restores the
* version the last update set aside. Exit codes: 0 = action taken / update
* available, 1 = up to date, 2 = error.
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
#include <fcntl.h>      // _O_CREAT / _O_RDWR for the install lock
#include <io.h>         // _sopen_s / _close
#include <share.h>      // _SH_DENYRW
#include <sys/stat.h>   // _S_IREAD / _S_IWRITE
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>   // fstat/stat, to re-check the locked inode
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#define EXIT_UPDATE_AVAILABLE 0
#define EXIT_UP_TO_DATE 1
#define EXIT_CHECK_ERROR 2

#define RELEASES_API_BASE "https://api.github.com/repos/objeck/objeck-lang/releases"

// The release signing key, in minisign's base64 form, matching
// core/release/objeck-release.pub -- the same bytes, compiled in so a user does
// not have to trust a file they downloaded alongside what it attests to.
//
// SHA256SUMS is served from the same place as the assets it describes, so
// anyone who can replace an asset can replace the manifest to match; the
// verification below then compares a substituted archive against a substituted
// manifest and reports success. The Linux and macOS archives carry no platform
// signature at all, so for those the manifest is the only integrity claim
// (docs/release_integrity.md).
//
// NOT YET ENFORCED. This is phase 2 of that design: releases are signed from
// now on, and phase 3 makes a missing or invalid signature fatal here. The
// ordering is deliberate rather than timid -- an obu that required a signature
// before any release carried one would reject every release it could see,
// including the ones already published.
//
// Rotation: ship an obu trusting both the old and new keys for one release,
// then drop the old. Keep the key id beside the key, because two base64 blobs
// are not something anyone can tell apart by eye.
#define OBU_RELEASE_KEY_ID "8237C6B17C7EA6B9"
#define OBU_RELEASE_PUBKEY "RWS5pn58scY3gv7X3EL/fJaOQli1V0HDwwFVt+2IWqPXBxvE4U1UhxHC"

// The release asset for this platform (see release-build.yml), the archive
// format it ships in -- Linux publishes .tgz, Windows and macOS .zip -- and the
// suffix the platform puts on an executable.
//
// macOS ships .zip rather than .tgz because only an archive Apple recognises
// (.zip, .pkg, .dmg) can be submitted to the notary service. An un-notarized
// download is quarantined, and a quarantined Objeck tree does not fail politely:
// obr is killed outright, with nothing on stdout or stderr. bsdtar reads a zip,
// so ArchiveTool() needs no change.
//
// Windows performs the in-place swap like every other platform. It needs no
// copy-self-and-re-exec dance: Windows permits RENAMING a running image (it
// only forbids deleting one), and the swap moves the current tree aside with
// fs::rename rather than deleting it, so the running obu.exe travels into
// .previous and keeps executing. The one consequence is that a tree still
// holding a running obu cannot be deleted; see StaleTreeResidue.
#if defined(_WIN32)
#if defined(_M_ARM64)
#define OBU_ASSET_PREFIX "objeck-windows-arm64"
#else
#define OBU_ASSET_PREFIX "objeck-windows-x64"
#endif
#define OBU_ASSET_SUFFIX ".zip"
#define OBU_EXE_SUFFIX ".exe"
#define OBU_UPDATE_SUPPORTED 1
#elif defined(__APPLE__)
#define OBU_ASSET_PREFIX "objeck-macos-arm64"
#define OBU_ASSET_SUFFIX ".zip"
#define OBU_EXE_SUFFIX ""
#define OBU_UPDATE_SUPPORTED 1
#elif defined(__linux__)
#if defined(__aarch64__)
#define OBU_ASSET_PREFIX "objeck-linux-arm64"
#else
#define OBU_ASSET_PREFIX "objeck-linux-x64"
#endif
#define OBU_ASSET_SUFFIX ".tgz"
#define OBU_EXE_SUFFIX ""
#define OBU_UPDATE_SUPPORTED 1
#else
#define OBU_ASSET_PREFIX ""
#define OBU_ASSET_SUFFIX ".tgz"
#define OBU_EXE_SUFFIX ""
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
  std::cout << "  rollback           restore the version kept by the last successful update" << std::endl;
  std::cout << "  verify <archive> <SHA256SUMS>" << std::endl;
  std::cout << "                     check a file you downloaded yourself against a manifest" << std::endl;
  std::cout << "                     (needs SHA256SUMS.minisig beside it: the manifest's" << std::endl;
  std::cout << "                     signature is checked before the manifest is read)" << std::endl << std::endl;
  std::cout << "Options for 'check' and 'update':" << std::endl;
  std::cout << "  --quiet            print nothing; communicate via the exit code" << std::endl;
  std::cout << "  --channel <tag>    target a specific release tag (e.g. v2026.8.0)" << std::endl;
  std::cout << "                     instead of the latest release" << std::endl;
  std::cout << "  --force            (update) install even if the tree is already current" << std::endl << std::endl;
  std::cout << "General options:" << std::endl;
  std::cout << "  --help             show this message" << std::endl;
  std::cout << "  --version          show the obu version" << std::endl << std::endl;
  std::cout << "Exit codes: 0 = action taken / update available, 1 = up to date, 2 = error" << std::endl;
  std::cout << "            'verify': 0 = matches, 1 = does NOT match, 2 = could not check" << std::endl;
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
* Fetches a release JSON document, honoring OBU_RELEASE_JSON_FILE for the
* offline test harness so the whole flow runs in CI without the network.
* 'source', when supplied, receives what the document was actually read from
* -- the hook's file or the request URL -- so a diagnostic can name it without
* claiming a network fetch that never happened.
*
* Both 'check' and 'update' come through here. They used to build this URL
* separately, which meant the hook covered 'update' only: 'check' always hit
* the live network, so on Windows -- where 'check' is the only supported
* command -- the test hooks had nothing to attach to at all.
****************************/
static bool FetchReleaseJson(const std::string& channel, bool is_quiet, std::string& json,
                             std::string& error, std::string* source = nullptr)
{
  // Validate the caller-supplied tag first, ahead of the hook below. The guard
  // is on user input, so it must not depend on where the document ends up
  // being read from -- and in particular the test hook must not be able to
  // skip it, which is what kept the guard from being exercised offline.
  if(!channel.empty() && !IsSafeTag(channel)) {
    error = "Invalid channel tag: '" + channel + '\'';
    return false;
  }

#ifdef OBU_TEST_HOOKS
  if(const char* json_file = std::getenv("OBU_RELEASE_JSON_FILE")) {
    std::ifstream in(json_file, std::ios::binary);
    if(!in) {
      error = std::string("Unable to read OBU_RELEASE_JSON_FILE: ") + json_file;
      return false;
    }
    json.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(source) { *source = json_file; }
    return true;
  }
#endif

  std::string url = RELEASES_API_BASE;
  url += channel.empty() ? "/latest" : "/tags/" + channel;
  if(source) { *source = url; }

  return FetchUrl(url, is_quiet, json, error);
}

/****************************
* 'check' command
****************************/
static int DoCheck(bool is_quiet, const std::string& channel)
{
  const std::string installed = InstalledVersion();

  std::string response, error, source;
  if(!FetchReleaseJson(channel, is_quiet, response, error, &source)) {
    if(!is_quiet) {
      std::cerr << error << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  std::string release_tag;
  if(!ExtractTagName(response, release_tag)) {
    if(!is_quiet) {
      std::cerr << "Unable to find \"tag_name\" in the release response from " << source << std::endl;
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
 * Update engine. Windows included: OBU_UPDATE_SUPPORTED is 1 for _WIN32 and
 * RunArgv has a CreateProcess path, so `obu update` runs on all three platforms.
 * This said "POSIX. Windows is deferred to phase 3" long after that stopped
 * being true, which reads as a supported platform being unsupported.
 * =================================================================== */

/****************************
* SHA-256, implemented inline so the integrity gate depends on nothing
* external -- an updater whose hash check can be shimmed by a missing or
* replaced system tool is not an integrity gate at all. Public-domain
* construction (FIPS 180-4).
****************************/

extern "C" {
#include "vendor/tweetnacl.h"
}

//
// TweetNaCl declares this and calls it in exactly two places, both key
// generation: crypto_box_keypair and crypto_sign_keypair. obu generates no
// keys, so this aborts rather than returning. A stub that quietly left the
// buffer untouched would hand a caller a "key" made of whatever was on the
// stack; failing loudly is the only safe shape for an unimplemented source of
// randomness. See vendor/README.md.
//
extern "C" void randombytes(unsigned char* buffer, unsigned long long length)
{
  (void)buffer;
  (void)length;
  std::cerr << "obu: randombytes was called, which means a key-generation path was reached. "
               "obu only verifies signatures and has no source of randomness." << std::endl;
  std::abort();
}

//
// Detached signature verification for SHA256SUMS (#723, docs/release_integrity.md)
//
// SHA256SUMS is served from the same place as the assets it describes, so
// anyone who can replace an asset can replace the manifest to match -- and the
// hash check below would then compare a substituted archive against a
// substituted manifest and report success. The Linux and macOS archives carry
// no platform signature at all, so for those the manifest is the only integrity
// claim. A detached Ed25519 signature is the part an attacker cannot reproduce
// without the release key.
//
// Ed25519 comes from vendor/tweetnacl.c, unmodified: see vendor/README.md for
// why it is not written here, how the download was corroborated, and what the
// randombytes stub below is for.
//
namespace minisig {
  // minisign's file layout, which this parses rather than assumes:
  //
  //   public key, 42 bytes   "Ed" + 8-byte key id + 32-byte key
  //   signature,  74 bytes   "Ed" + 8-byte key id + 64-byte signature
  //
  // Releases are signed with `minisign -S -l`, the legacy format, so the
  // signature is over the RAW manifest. minisign's default prehashed mode is
  // "ED" and signs BLAKE2b-512 of it, which would have meant a second
  // hand-written primitive in the trust path; see the correction in
  // release_integrity.md. "ED" is therefore rejected here rather than
  // mishandled -- a signature we cannot check must never read as one we did.
  static const size_t PUBKEY_BYTES = 42;
  static const size_t SIGNATURE_BYTES = 74;
  static const size_t ED25519_SIG_BYTES = 64;
  static const size_t ED25519_KEY_BYTES = 32;
  static const size_t KEY_ID_BYTES = 8;

  static bool Base64Decode(const std::string& text, std::vector<unsigned char>& out)
  {
    static const char* ALPHABET =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    out.clear();
    unsigned int accum = 0;
    int bits = 0;
    size_t padding = 0;

    for(size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      if(c == '\r' || c == '\n' || c == ' ' || c == '\t') {
        continue;
      }
      if(c == '=') {
        ++padding;
        continue;
      }
      // Padding is only ever trailing. A '=' followed by data is malformed, and
      // skipping it quietly would let two different texts decode alike.
      if(padding) {
        return false;
      }
      const char* at = strchr(ALPHABET, c);
      if(!at || c == '\0') {
        return false;
      }
      accum = (accum << 6) | (unsigned int)(at - ALPHABET);
      bits += 6;
      if(bits >= 8) {
        bits -= 8;
        out.push_back((unsigned char)((accum >> bits) & 0xFF));
      }
    }

    // Leftover bits must be zero: a trailing group that carries value is a
    // different string than it appears to be.
    if(bits && ((accum & ((1u << bits) - 1)) != 0)) {
      return false;
    }
    return padding <= 2;
  }

  // The second line of a minisign file, decoded. Both the key and the signature
  // files put their payload there; everything else is comments.
  static bool SecondLinePayload(const std::string& text, std::vector<unsigned char>& out,
                                std::string& trusted_comment, std::vector<unsigned char>& global_sig)
  {
    std::vector<std::string> lines;
    std::string line;
    for(size_t i = 0; i <= text.size(); ++i) {
      if(i == text.size() || text[i] == '\n') {
        while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
          line.pop_back();
        }
        if(!line.empty()) {
          lines.push_back(line);
        }
        line.clear();
      }
      else {
        line.push_back(text[i]);
      }
    }

    if(lines.size() < 2) {
      return false;
    }
    if(!Base64Decode(lines[1], out)) {
      return false;
    }

    // A signature file carries two more lines: the trusted comment and a second
    // signature over (signature || trusted comment). Verifying that one is what
    // makes the comment trustworthy -- it names the file the signature is for,
    // so without it a signature for one file could be presented with a comment
    // claiming another.
    trusted_comment.clear();
    global_sig.clear();
    if(lines.size() >= 4) {
      static const char* PREFIX = "trusted comment: ";
      const size_t prefix_len = strlen(PREFIX);
      if(lines[2].compare(0, prefix_len, PREFIX) == 0) {
        trusted_comment = lines[2].substr(prefix_len);
      }
      if(!Base64Decode(lines[3], global_sig)) {
        return false;
      }
    }
    return true;
  }

  //
  // Whether 'message' carries a valid signature from the compiled-in release key.
  //
  // 'reason' is set on every false return. The caller prints it: a verification
  // that fails without saying why leaves a user unable to tell a corrupted
  // download from a substituted one from a bug here.
  //
  static bool Verify(const std::string& message, const std::string& signature_file,
                     const std::string& pubkey_b64, std::string& reason,
                     std::string& trusted_comment)
  {
    std::vector<unsigned char> pubkey;
    if(!Base64Decode(pubkey_b64, pubkey) || pubkey.size() != PUBKEY_BYTES) {
      reason = "the release public key compiled into this obu is malformed";
      return false;
    }

    std::vector<unsigned char> sig, global_sig;
    if(!SecondLinePayload(signature_file, sig, trusted_comment, global_sig)) {
      reason = "the signature file could not be parsed";
      return false;
    }
    if(sig.size() != SIGNATURE_BYTES) {
      reason = "the signature is " + std::to_string(sig.size()) + " bytes, expected "
             + std::to_string(SIGNATURE_BYTES);
      return false;
    }

    if(sig[0] != 'E' || sig[1] != 'd') {
      // "ED" is minisign's prehashed mode, which signs BLAKE2b-512 of the
      // message. obu cannot check that and must not pretend otherwise.
      const std::string alg(1, (char)sig[0]);
      reason = "unsupported signature algorithm '" + alg + std::string(1, (char)sig[1])
             + "'; this obu verifies the legacy 'Ed' form only";
      return false;
    }
    if(pubkey[0] != 'E' || pubkey[1] != 'd') {
      reason = "the compiled-in public key is not an Ed25519 key";
      return false;
    }

    // A signature made by a different key is a wrong key, not a bad signature,
    // and saying so saves someone debugging the wrong thing.
    if(memcmp(&sig[2], &pubkey[2], KEY_ID_BYTES) != 0) {
      reason = "the signature was made by a different key than this obu trusts";
      return false;
    }

    const unsigned char* key = &pubkey[2 + KEY_ID_BYTES];

    // TweetNaCl verifies a signed message: signature followed by the message,
    // and it writes the recovered message out, so the buffer has to hold both.
    std::vector<unsigned char> signed_message(ED25519_SIG_BYTES + message.size());
    memcpy(&signed_message[0], &sig[2 + KEY_ID_BYTES], ED25519_SIG_BYTES);
    if(!message.empty()) {
      memcpy(&signed_message[ED25519_SIG_BYTES], message.data(), message.size());
    }

    std::vector<unsigned char> recovered(signed_message.size());
    unsigned long long recovered_len = 0;
    if(crypto_sign_open(&recovered[0], &recovered_len, &signed_message[0],
                        (unsigned long long)signed_message.size(), key) != 0) {
      reason = "the signature does not match the manifest";
      return false;
    }

    // The global signature binds the trusted comment to this signature. Without
    // checking it the comment is attacker-controlled text that obu would print
    // as though the key had vouched for it.
    if(global_sig.empty()) {
      reason = "the signature file has no global signature, so its trusted comment is unauthenticated";
      return false;
    }
    if(global_sig.size() != ED25519_SIG_BYTES) {
      reason = "the global signature is " + std::to_string(global_sig.size())
             + " bytes, expected " + std::to_string(ED25519_SIG_BYTES);
      return false;
    }

    std::vector<unsigned char> global_body;
    global_body.insert(global_body.end(), &sig[2 + KEY_ID_BYTES],
                       &sig[2 + KEY_ID_BYTES] + ED25519_SIG_BYTES);
    global_body.insert(global_body.end(), trusted_comment.begin(), trusted_comment.end());

    std::vector<unsigned char> global_signed(ED25519_SIG_BYTES + global_body.size());
    memcpy(&global_signed[0], &global_sig[0], ED25519_SIG_BYTES);
    if(!global_body.empty()) {
      memcpy(&global_signed[ED25519_SIG_BYTES], &global_body[0], global_body.size());
    }

    std::vector<unsigned char> global_recovered(global_signed.size());
    unsigned long long global_recovered_len = 0;
    if(crypto_sign_open(&global_recovered[0], &global_recovered_len, &global_signed[0],
                        (unsigned long long)global_signed.size(), key) != 0) {
      reason = "the trusted comment's signature does not match";
      return false;
    }

    reason.clear();
    return true;
  }
}

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
* Returns the child exit code, or OBU_SPAWN_FAILED if it could not be launched.
****************************/
#ifdef _WIN32
static int RunArgv(const std::vector<std::string>& args, bool quiet)
{
  if(args.empty()) {
    return OBU_SPAWN_FAILED;
  }

  std::string command_line;
  for(size_t i = 0; i < args.size(); ++i) {
    if(i > 0) { command_line += ' '; }
    command_line += QuoteWindowsArg(args[i]);
  }
  std::vector<char> writable(command_line.begin(), command_line.end());
  writable.push_back('\0');

  SECURITY_ATTRIBUTES attrs;
  attrs.nLength = sizeof(attrs);
  attrs.lpSecurityDescriptor = nullptr;
  attrs.bInheritHandle = TRUE;

  HANDLE null_out = INVALID_HANDLE_VALUE;
  if(quiet) {
    null_out = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                           &attrs, OPEN_EXISTING, 0, nullptr);
  }

  STARTUPINFOA start;
  ZeroMemory(&start, sizeof(start));
  start.cb = sizeof(start);
  if(null_out != INVALID_HANDLE_VALUE) {
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = null_out;
    start.hStdError = null_out;
  }

  PROCESS_INFORMATION proc;
  ZeroMemory(&proc, sizeof(proc));

  // lpApplicationName is null so PATH is searched, but no shell is spawned
  const BOOL launched = CreateProcessA(nullptr, writable.data(), nullptr, nullptr,
                                       TRUE, 0, nullptr, nullptr, &start, &proc);
  if(null_out != INVALID_HANDLE_VALUE) { CloseHandle(null_out); }
  if(!launched) {
    return OBU_SPAWN_FAILED;
  }

  WaitForSingleObject(proc.hProcess, INFINITE);
  DWORD status = 0;
  if(!GetExitCodeProcess(proc.hProcess, &status)) {
    status = static_cast<DWORD>(OBU_SPAWN_FAILED);
  }
  CloseHandle(proc.hProcess);
  CloseHandle(proc.hThread);

  return static_cast<int>(status);
}
#else
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
    _exit(OBU_SPAWN_FAILED);
  }

  int status = 0;
  while(waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    /* retry */
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : OBU_SPAWN_FAILED;
}
#endif  // _WIN32

/****************************
* bsdtar ships in System32 on Windows 10 1803+ and reads .zip as well as .tgz.
* Resolve it by absolute path rather than trusting PATH, where an MSYS/GNU tar
* -- which cannot read a zip at all -- may shadow it.
****************************/
static std::string ArchiveTool()
{
#ifdef _WIN32
  char system_dir[MAX_PATH];
  const UINT length = GetSystemDirectoryA(system_dir, MAX_PATH);
  if(length > 0 && length < MAX_PATH) {
    std::error_code ec;
    const fs::path candidate = fs::path(system_dir) / "tar.exe";
    if(fs::exists(candidate, ec)) {
      return candidate.string();
    }
  }
#endif
  return "tar";
}

/****************************
* An archive member must be a relative path that stays inside the staging dir.
* Rejects absolute paths, drive-qualified paths and any '..' component, in both
* separator conventions -- a zip may legitimately carry backslashes, so the
* Windows form has to be rejected too (zip-slip).
****************************/
static bool IsUnsafeArchiveMember(const std::string& member)
{
  if(member.empty()) {
    return false;
  }
  if(member.front() == '/' || member.front() == '\\') {
    return true;
  }
  if(member.size() >= 2 && member[1] == ':') {   // C:\... or C:/...
    return true;
  }

  // compare whole components, so a filename that merely contains dots ("a..b")
  // is not mistaken for a traversal
  size_t start = 0;
  while(start <= member.size()) {
    size_t end = member.find_first_of("/\\", start);
    if(end == std::string::npos) { end = member.size(); }
    if(member.compare(start, end - start, "..") == 0) {
      return true;
    }
    start = end + 1;
  }
  return false;
}

/****************************
* Best-effort removal of a tree that may still hold a running image. Windows
* lets a running .exe be renamed but never deleted, so the tree obu itself was
* launched from survives this call; the next run clears it, once nothing is
* executing out of it. Returns true if anything was left behind.
****************************/
static bool StaleTreeResidue(const fs::path& tree)
{
  std::error_code ec;
  fs::remove_all(tree, ec);
  return fs::exists(tree, ec);
}
#endif  // OBU_UPDATE_SUPPORTED

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

    // Only accept an https URL on a GitHub host. The download itself no longer
    // goes through a shell (see FetchUrl/DownloadAsset), so this is not the
    // injection defense it once was -- it now keeps a tampered release from
    // pointing the download at an arbitrary host.
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
//
// Whether 'sums' carries a valid signature from the release key.
//
// Called before ANY line of the manifest is trusted, by both `update` and
// `verify`. It is one function for both so the two cannot drift: an updater
// that checks and a verify subcommand that does not would be worse than
// neither, because a user would believe the latter.
//
// Phase 3 of docs/release_integrity.md: a missing or invalid signature is FATAL.
//
static bool VerifyManifestSignature(const std::string& sums, const fs::path& sig_path,
                                    bool is_quiet, std::string& trusted_comment)
{
  std::error_code ec;
  if(!fs::is_regular_file(sig_path, ec)) {
    if(!is_quiet) {
      std::cerr << "No signature beside the manifest ("
                << sig_path.filename().string() << ")." << std::endl
                << "SHA256SUMS is served from the same place as the assets it describes, so"
                << std::endl
                << "without a signature it shows only that the download was not corrupted --"
                << std::endl
                << "not that this release came from the Objeck maintainer." << std::endl;
    }
    return false;
  }

  std::ifstream sig_in(sig_path, std::ios::binary);
  if(!sig_in) {
    if(!is_quiet) {
      std::cerr << "Could not read " << sig_path.string() << std::endl;
    }
    return false;
  }
  const std::string sig_text((std::istreambuf_iterator<char>(sig_in)),
                             std::istreambuf_iterator<char>());

  // The key this obu trusts. A release build has exactly one, compiled in.
  std::string trusted_key = OBU_RELEASE_PUBKEY;
#ifdef OBU_TEST_HOOKS
  // Test builds only, and gated the same way as every other hook here:
  // test_update.py generates fake releases whose manifests carry computed
  // hashes, so no committed signature could ever match them, and it has no
  // access to the release secret. It signs with a throwaway key and names it
  // here. The shipped binary is built WITHOUT OBU_TEST_HOOKS, so there is no
  // variable for it to read -- an override that survived into a release would
  // be a bypass for the check this function exists to perform.
  if(const char* key_override = std::getenv("OBU_TRUSTED_PUBKEY")) {
    trusted_key = key_override;
  }
#endif

  std::string reason;
  if(!minisig::Verify(sums, sig_text, trusted_key, reason, trusted_comment)) {
    if(!is_quiet) {
      std::cerr << "SIGNATURE VERIFICATION FAILED: " << reason << std::endl
                << "Refusing to trust this manifest. The release key is "
                << OBU_RELEASE_KEY_ID << "; its public half is published in README.md"
                << std::endl
                << "and on objeck.org, so it can be checked out of band." << std::endl;
    }
    return false;
  }

  if(!is_quiet) {
    std::cout << "Signature verified (key " << OBU_RELEASE_KEY_ID << ")" << std::endl;
    if(!trusted_comment.empty()) {
      std::cout << "  " << trusted_comment << std::endl;
    }
  }
  return true;
}

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

// obu's own scratch entries at the top of the install root. The swap never
// moves or deletes these.
//
// .obu.lock belongs in this list: obu holds it open for the whole operation,
// and Windows refuses to rename a file that is open (a running .exe can be
// renamed, an open handle without FILE_SHARE_DELETE cannot), so the swap died
// on it. It was wrong on POSIX too -- harmlessly, but the lock ended up buried
// inside .previous instead of staying in the root.
static bool IsObuScratchEntry(const std::string& name)
{
  return name == ".obu-work" || name == ".previous" ||
         name == ".obu-rollback" || name == ".obu.lock";
}

// The platform call rather than std::this_thread: neither obu Makefile nor the
// test harness passes -pthread, and on a glibc older than 2.34 that is a link
// error waiting for whoever builds the updater on an older distro.
// <windows.h> and <unistd.h> are already included above.
static void SleepBriefly(int ms)
{
#if defined(_WIN32)
  Sleep(static_cast<DWORD>(ms));
#else
  usleep(static_cast<useconds_t>(ms) * 1000);
#endif
}

/****************************
* fs::rename, retried briefly while something else still holds the entry.
*
* Windows denies the rename of a directory while any other process holds a
* handle inside it -- antivirus scanning a binary whose bytes just changed,
* the indexer, or a just-exited child whose handle has not been reaped. The
* holder is gone within milliseconds, but a single attempt sees only
* "Access is denied" and the entire update unwinds.
*
* Observed in CI on windows-x64: the swap failed moving 'bin', the directory
* holding the running obu.exe, and the identical code passed on the next run.
* A user running 'obu update' on Windows hits the same race, and the only
* recourse is to run it again.
*
* The retry is deliberately not conditioned on a particular error code. Every
* caller treats a failure as fatal and unwinds, so a few hundred milliseconds
* spent before giving up costs nothing on a genuinely permanent error, and
* the error finally reported is the one the last attempt produced.
****************************/
static void RenameWithRetry(const fs::path& from, const fs::path& to, std::error_code& ec)
{
  const int attempts = 6;
  int wait_ms = 10;

  for(int i = 0; i < attempts; i++) {
    ec.clear();
    fs::rename(from, to, ec);
    if(!ec) {
      return;
    }

    if(i + 1 < attempts) {
      SleepBriefly(wait_ms);
      wait_ms *= 2;
    }
  }
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
    if(IsObuScratchEntry(name)) {
      continue;
    }
    RenameWithRetry(path, to / name, ec);
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
    if(!IsObuScratchEntry(path.filename().string())) {
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
#ifdef _WIN32
  // _SH_DENYRW gives the same semantics flock(LOCK_EX | LOCK_NB) does below:
  // a second obu fails to open it at all rather than waiting. Windows has no
  // flock, and _locking() would need the region locked separately.
  int fd = -1;
  if(_sopen_s(&fd, lock_path.c_str(), _O_CREAT | _O_RDWR, _SH_DENYRW,
              _S_IREAD | _S_IWRITE) != 0) {
    return -1;
  }
  return fd;
#else
  // ReleaseLock unlinks the file, which makes the naive open+flock unsafe: a
  // second obu can open the inode, the holder can then unlink it, and the
  // second obu's flock succeeds on a file that is no longer at lock_path --
  // while a third obu creates a fresh one and locks that. Two winners.
  //
  // Re-check, after locking, that the inode we hold is still the one the path
  // names. If it is not, the file was replaced under us; drop it and retry.
  for(int attempt = 0; attempt < 4; ++attempt) {
    const int fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
    if(fd < 0) {
      return -1;
    }
    if(flock(fd, LOCK_EX | LOCK_NB) != 0) {
      close(fd);
      return -1;   // another obu holds it
    }

    struct stat held {}, named {};
    if(fstat(fd, &held) != 0) {
      close(fd);
      return -1;
    }
    if(stat(lock_path.c_str(), &named) == 0 &&
       held.st_dev == named.st_dev && held.st_ino == named.st_ino) {
      return fd;
    }
    close(fd);   // unlinked or replaced between our open and our lock
  }
  return -1;
#endif
}

// Releases the lock and removes the file, so a finished obu leaves nothing
// behind in the user's install root (#931).
//
// On POSIX the unlink happens while the lock is still held, so a process that
// opened the old inode cannot then acquire it and believe it owns a lock on a
// file that no longer exists -- AcquireLock's inode re-check closes the rest of
// that window. On Windows exclusion comes from the _SH_DENYRW share mode rather
// than from the inode, and a file another obu has open cannot be deleted at
// all, so the failure to remove it there is the correct outcome and is ignored.
//
// MSVC spells close() as _close and only exposes the unprefixed name as a
// deprecated alias.
static void ReleaseLock(int fd, const fs::path& root)
{
  if(fd < 0) {
    return;
  }
  const fs::path lock_path = root / ".obu.lock";
#ifdef _WIN32
  _close(fd);
  std::error_code ec;
  fs::remove(lock_path, ec);
#else
  std::error_code ec;
  fs::remove(lock_path, ec);
  close(fd);
#endif
}

/****************************
* If a previous run died mid-swap -- current tree archived to .previous but the
* new payload not yet installed -- root is left without bin/. Detect that and
* complete the interrupted rollback so the install is never left unusable.
****************************/
static bool RecoverInterruptedSwap(const fs::path& root, bool is_quiet)
{
  std::error_code ec;
  // A prior rollback may have been unable to delete the tree it set aside,
  // because obu was running out of it at the time (Windows forbids deleting a
  // running image). Nothing is executing there now, so clear it first.
  fs::remove_all(root / ".obu-rollback", ec);

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
    ReleaseLock(lock_fd, root);
    return EXIT_CHECK_ERROR;
  }

  const fs::path work = root / ".obu-work";
  const fs::path previous = root / ".previous";
  const fs::path staging = work / "staging";

  // A single cleanup+unlock path so no early return leaks staging or the lock.
  struct Guard {
    const fs::path& work; const fs::path& root; int fd; bool armed = true;
    ~Guard() { if(armed) { std::error_code e; fs::remove_all(work, e); } ReleaseLock(fd, root); }
  } guard{work, root, lock_fd};

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
  if(!ExtractAssetUrl(json, OBU_ASSET_PREFIX, OBU_ASSET_SUFFIX, asset_url, asset_name)) {
    std::cerr << "Release " << release_tag << " has no " OBU_ASSET_PREFIX " asset "
                 "(a macOS notarization gap does this -- try again once it publishes)." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  if(!IsSafeAssetName(asset_name)) {
    std::cerr << "Refusing an asset with an unexpected name: '" << asset_name << "'." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  // Optional at the HTTP level, mandatory at the trust level: a release with no
  // signature asset reaches VerifyManifestSignature and is refused there.
  std::string sig_url, sig_name;
  ExtractAssetUrl(json, "SHA256SUMS.minisig", "", sig_url, sig_name);

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
  const fs::path asset_path = work / ("asset" OBU_ASSET_SUFFIX);
  const fs::path sums_path = work / "SHA256SUMS";
  const fs::path sig_path = work / "SHA256SUMS.minisig";
  if(!DownloadAsset(asset_url, asset_name, asset_path, is_quiet, error) ||
     !DownloadAsset(sums_url, sums_name, sums_path, is_quiet, error)) {
    std::cerr << error << std::endl;
    return EXIT_CHECK_ERROR;
  }

  // The signature is a separate asset. A release without one fails in
  // VerifyManifestSignature rather than here, so the message can say what is
  // missing and why it matters instead of reading as a transfer error.
  if(sig_url.empty() || !DownloadAsset(sig_url, sig_name, sig_path, is_quiet, error)) {
    std::error_code sig_ec;
    fs::remove(sig_path, sig_ec);
  }

  // VERIFY BEFORE ANYTHING IS TOUCHED, and verify the manifest's SIGNATURE
  // before any line of the manifest itself. The hash check below shows the
  // download was not corrupted; on its own it does not show the release came
  // from the maintainer, because SHA256SUMS ships on the same channel as the
  // asset and whoever can replace one can replace the other. The signature is
  // the part that cannot be reproduced without the release key (#723).
  std::ifstream sums_in(sums_path, std::ios::binary);
  std::string sums((std::istreambuf_iterator<char>(sums_in)), std::istreambuf_iterator<char>());

  std::string trusted_comment;
  if(!VerifyManifestSignature(sums, sig_path, is_quiet, trusted_comment)) {
    return EXIT_CHECK_ERROR;
  }

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

  // reject an archive with an absolute or traversing member before extracting.
  // Windows ships .zip, so the flags differ: bsdtar infers zip without -z, and
  // --no-same-owner is a POSIX ownership concern that does not apply.
  const std::string tar = ArchiveTool();
#ifdef _WIN32
  const std::vector<std::string> list_args = {tar, "-tf", asset_path.string()};
  const std::vector<std::string> extract_args = {tar, "-xf", asset_path.string(),
                                                 "-C", staging.string()};
#else
  const std::vector<std::string> list_args = {tar, "-tzf", asset_path.string()};
  const std::vector<std::string> extract_args = {tar, "--no-same-owner", "-xzf",
                                                 asset_path.string(), "-C", staging.string()};
#endif

  std::string listing;
  if(!RunArgvCapture(list_args, listing)) {
    std::cerr << "Unable to read the archive " << asset_name << "." << std::endl;
    return EXIT_CHECK_ERROR;
  }
  for(size_t p = 0; p < listing.size();) {
    size_t eol = listing.find('\n', p);
    if(eol == std::string::npos) { eol = listing.size(); }
    std::string member = listing.substr(p, eol - p);
    p = eol + 1;
    if(!member.empty() && member.back() == '\r') {   // bsdtar on Windows
      member.pop_back();
    }
    if(IsUnsafeArchiveMember(member)) {
      std::cerr << "Archive contains an unsafe path ('" << member << "'); refusing." << std::endl;
      return EXIT_CHECK_ERROR;
    }
  }

  // extract into a fresh staging dir (never into the live tree)
  fs::create_directories(staging, ec);
  if(RunArgv(extract_args, is_quiet) != 0) {
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
    if(!MoveTreeEntries(previous, root, error)) {
      std::cerr << "RESTORE ALSO FAILED: " << error << ". Your install is split between the root and "
                << previous << "; move the contents of that directory back by hand." << std::endl;
      return EXIT_CHECK_ERROR;   // keep .previous: it holds the only copy
    }
    fs::remove_all(previous, ec);
    return EXIT_CHECK_ERROR;
  }

  // POST-CHECK: the design names 'obc -v' as authoritative for what is
  // installed. On failure, roll back automatically. ('obr' has no version
  // flag, so it must not be used here.)
  const int health = RunArgv({(root / "bin" / ("obc" OBU_EXE_SUFFIX)).string(), "-v"}, is_quiet);
  if(health != 0) {
    std::cerr << "The updated Objeck failed its post-install check; rolling back." << std::endl;
    RemoveManagedEntries(root);
    if(!MoveTreeEntries(previous, root, error)) {
      std::cerr << "RESTORE ALSO FAILED: " << error << ". Your install is split between the root and "
                << previous << "; move the contents of that directory back by hand." << std::endl;
      return EXIT_CHECK_ERROR;   // keep .previous: it holds the only copy
    }
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
/****************************
* Verifies a downloaded archive against a SHA256SUMS manifest.
*
* `update` already does exactly this before it touches anything, but someone
* who downloaded with curl has no way to run the same check. This exposes it.
*
* It introduces NO new trust. SHA256SUMS ships from the same place as the asset
* it describes, so a match proves the download was not corrupted or truncated
* -- it does not prove the release was published by a trusted party, because
* anyone who can replace an asset can replace the manifest to match. Detecting
* a substituted manifest needs a signature over SHA256SUMS, which is phases 2-4
* of docs/release_integrity.md and requires a maintainer-held key.
*
* Exit codes follow the rest of obu: 0 the file matches, 1 it does not, 2 the
* check could not be carried out. A mismatch is deliberately distinct from an
* error, so a script can tell "this file is wrong" from "I could not tell".
****************************/
static int DoVerify(const std::string& archive_arg, const std::string& sums_arg, bool is_quiet)
{
  const fs::path archive(archive_arg);
  const fs::path sums_path(sums_arg);

  std::error_code ec;
  if(!fs::is_regular_file(archive, ec)) {
    if(!is_quiet) {
      std::cerr << "No such file: " << archive_arg << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  if(!fs::is_regular_file(sums_path, ec)) {
    if(!is_quiet) {
      std::cerr << "No such file: " << sums_arg << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  std::ifstream sums_in(sums_path, std::ios::binary);
  if(!sums_in) {
    if(!is_quiet) {
      std::cerr << "Could not read " << sums_arg << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }
  const std::string sums((std::istreambuf_iterator<char>(sums_in)), std::istreambuf_iterator<char>());

  // The same gate as `update`, through the same function, for the same reason: a
  // user who fetched with curl and runs `obu verify` is asking the question
  // `update` asks and must not get a weaker answer. The signature is looked for
  // beside the manifest, which is where minisign writes it and where a release
  // publishes it.
  std::string trusted_comment;
  if(!VerifyManifestSignature(sums, fs::path(sums_arg + ".minisig"), is_quiet, trusted_comment)) {
    return EXIT_CHECK_ERROR;
  }

  // The manifest lists bare names, so look the archive up by its file name and
  // not by whatever path the caller typed.
  const std::string name = archive.filename().string();
  std::string expected;
  if(!ExpectedHash(sums, name, expected)) {
    if(!is_quiet) {
      std::cerr << name << " is not listed in " << sums_arg << "; cannot verify it." << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  const std::string actual = Sha256File(archive);
  if(actual.empty()) {
    if(!is_quiet) {
      std::cerr << "Could not hash " << archive_arg << std::endl;
    }
    return EXIT_CHECK_ERROR;
  }

  if(actual != expected) {
    if(!is_quiet) {
      std::cerr << "Integrity check FAILED for " << name << " (expected " << expected
                << ", got " << actual << ")." << std::endl;
    }
    return EXIT_UP_TO_DATE;   // 1: a definite mismatch, not an error
  }

  if(!is_quiet) {
    std::cout << "Verified " << name << " against " << sums_arg << "." << std::endl;
  }

  return EXIT_UPDATE_AVAILABLE;   // 0: the file is what the manifest says
}

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
    ReleaseLock(lock_fd, root);
    return EXIT_CHECK_ERROR;
  }

  // current -> holding, previous -> root, then discard holding
  const fs::path holding = root / ".obu-rollback";
  fs::remove_all(holding, ec);
  std::string error;
  if(!MoveTreeEntries(root, holding, error)) {
    std::cerr << "Rollback failed while setting aside the current version: " << error << std::endl;
    MoveTreeEntries(holding, root, error);
    ReleaseLock(lock_fd, root);
    return EXIT_CHECK_ERROR;
  }
  if(!MoveTreeEntries(previous, root, error)) {
    std::cerr << "Rollback failed while restoring: " << error << ". Attempting to undo." << std::endl;
    RemoveManagedEntries(root);
    MoveTreeEntries(holding, root, error);
    ReleaseLock(lock_fd, root);
    return EXIT_CHECK_ERROR;
  }
  // The version we rolled back FROM is discarded. On Windows the running
  // obu.exe now lives in there -- it was renamed aside, which Windows allows --
  // and a running image cannot be deleted, so this can legitimately leave the
  // tree behind. RecoverInterruptedSwap clears it on the next run, by which
  // point nothing is executing out of it.
  if(StaleTreeResidue(holding) && !is_quiet) {
    std::cout << "Note: the replaced version could not be fully removed while obu is "
                 "running from it; it will be cleaned up on the next run." << std::endl;
  }
  fs::remove_all(previous, ec);

  const int health = RunArgv({(root / "bin" / ("obc" OBU_EXE_SUFFIX)).string(), "-v"}, is_quiet);
  ReleaseLock(lock_fd, root);
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

  if(command == "verify") {
    std::vector<std::string> operands;
    bool verify_quiet = false;
    for(int i = 2; i < argc; ++i) {
      const std::string option = argv[i];
      if(option == "--quiet" || option == "-q") {
        verify_quiet = true;
      }
      else if(!option.empty() && option[0] == '-') {
        std::cerr << "Unknown option for 'verify': '" << option << "'" << std::endl;
        return EXIT_CHECK_ERROR;
      }
      else {
        operands.push_back(option);
      }
    }

    if(operands.size() != 2) {
      std::cerr << "Usage: obu verify <archive> <SHA256SUMS>" << std::endl;
      return EXIT_CHECK_ERROR;
    }

    return DoVerify(operands[0], operands[1], verify_quiet);
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
