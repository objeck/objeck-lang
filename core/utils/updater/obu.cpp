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

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define OBU_POPEN _popen
#define OBU_PCLOSE _pclose
#else
#include <sys/wait.h>
#define OBU_POPEN popen
#define OBU_PCLOSE pclose
#endif

#define EXIT_UPDATE_AVAILABLE 0
#define EXIT_UP_TO_DATE 1
#define EXIT_CHECK_ERROR 2

#define RELEASES_API_BASE "https://api.github.com/repos/objeck/objeck-lang/releases"

/****************************
* Usage
****************************/
static void Usage()
{
  std::cout << "Usage: obu <command> [options]" << std::endl << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "  check              check whether a newer Objeck release is available" << std::endl << std::endl;
  std::cout << "Options for 'check':" << std::endl;
  std::cout << "  --quiet            print nothing; communicate via the exit code" << std::endl;
  std::cout << "  --channel <tag>    compare against a specific release tag (e.g. v2026.8.0)" << std::endl;
  std::cout << "                     instead of the latest release" << std::endl << std::endl;
  std::cout << "General options:" << std::endl;
  std::cout << "  --help             show this message" << std::endl;
  std::cout << "  --version          show the obu version" << std::endl << std::endl;
  std::cout << "Exit codes for 'check': 0 = update available, 1 = up to date, 2 = error" << std::endl;
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
* Ensures a tag is safe to place
* in a shell command line
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

/****************************
* Fetches a URL by shelling out
* to the system 'curl' binary
****************************/
static bool FetchUrl(const std::string& url, bool is_quiet, std::string& response, std::string& error)
{
  std::string command = "curl -fsSL --max-time 20 \"" + url + '"';
  if(is_quiet) {
#ifdef _WIN32
    command += " 2>nul";
#else
    command += " 2>/dev/null";
#endif
  }

  FILE* pipe = OBU_POPEN(command.c_str(), "r");
  if(!pipe) {
    error = "Unable to run 'curl'; please ensure it is installed and on the path.";
    return false;
  }

  response.clear();
  char buffer[4096];
  size_t read;
  while((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
    response.append(buffer, read);
  }

  const int status = OBU_PCLOSE(pipe);
#ifdef _WIN32
  const int exit_code = status;
#else
  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

  if(status == -1 || exit_code != 0) {
    error = "Request failed: " + url + " ('curl' exited with code " +
      std::to_string(exit_code) + "; is curl installed and the network reachable?)";
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

  if(command != "check") {
    std::cerr << "Unknown command: '" << command << '\'' << std::endl << std::endl;
    Usage();
    return EXIT_CHECK_ERROR;
  }

  bool is_quiet = false;
  std::string channel;
  for(int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    if(option == "--quiet" || option == "-q") {
      is_quiet = true;
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

  return DoCheck(is_quiet, channel);
}
