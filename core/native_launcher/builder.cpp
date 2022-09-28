/***************************************************************************
 * Builder for native execution environment
 *
 * Copyright (c) 2022, Randy Hollines
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
 * contributors may be used to endorse or promote prod*ucts derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOTre
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

#include "builder.h"

int main(int argc, char* argv[])
{
  // get command line parameters
  list<wstring> argument_options;
  map<const wstring, wstring> cmd_params = ParseCommnadLine(argc, argv, argument_options);
  if(cmd_params.size() < 3) {
    wcout << GetUsage() << endl;
    exit(1);
  }
  
  wstring runtime_base_dir = GetCommandParameter(L"install", cmd_params, argument_options, true);
  wstring to_base_dir = GetCommandParameter(L"to_dir", cmd_params, argument_options);
  const wstring to_name = GetCommandParameter(L"to_name", cmd_params, argument_options);
  const wstring src_obe_file = GetCommandParameter(L"src_file", cmd_params, argument_options);
  const wstring src_dir = GetCommandParameter(L"src_dir", cmd_params, argument_options, true);

  // check command line parameters
  if(!EndsWith(src_obe_file, L".obe")) {
    wcout << GetUsage() << endl;
    exit(1);
  }

  if(runtime_base_dir.empty()) {
    runtime_base_dir = GetInstallDirectory();
  }

  // check for required parameters
  if(argument_options.empty()) {
    try {
      bool is_ok = true;

      to_base_dir += fs::path::preferred_separator;
      to_base_dir += to_name;

      // check files and directories
      fs::path src_obe_path(src_obe_file);
      if(!fs::exists(src_obe_path)) {
        is_ok = false;
        wcerr << ">>> Unable to find source file '" << src_obe_path << L"' <<<" << endl;
      }

      fs::path src_dir_path(src_dir);
      if(!src_dir_path.empty() && !fs::exists(src_dir_path)) {
        is_ok = false;
        wcerr << ">>> Unable to find source directory '" << src_dir_path << L"' <<<" << endl;
      }

      if(fs::exists(to_base_dir)) {
        wcerr << ">>> Target directory '" << to_base_dir << L"' already exists <<<" << endl;
        is_ok = false;
      }

      if(!CheckInstallDir(runtime_base_dir)) {
        wcerr << ">>> Invalid Objeck install directory: '" << runtime_base_dir << L"' <<<" << endl;
        is_ok = false;
      }

      if(!is_ok) {
        exit(1);
      }
      
      // create target directory
      fs::create_directory(to_base_dir);
      
      fs::path to_runtime_path(to_base_dir);
      to_runtime_path += fs::path::preferred_separator;
      to_runtime_path += L"runtime";
      fs::create_directory(to_runtime_path);

      fs::path to_app_path(to_base_dir);
      to_app_path += fs::path::preferred_separator;
      to_app_path += L"app";      
      fs::create_directory(to_app_path);

      // copy 'bin' directory
      fs::path runtime_bin_path(runtime_base_dir);
      runtime_bin_path += fs::path::preferred_separator;
      runtime_bin_path += L"bin";      
      fs::path from_bin_path(runtime_bin_path);

      fs::path to_bin_path(to_base_dir);
      to_bin_path += fs::path::preferred_separator;
      to_bin_path += L"runtime";    
      to_bin_path += fs::path::preferred_separator;
      to_bin_path += L"bin";      
      fs::create_directory(to_bin_path);

      fs::copy(from_bin_path, to_bin_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);

      // delete unneeded binaries
#ifdef _WIN32
      fs::path to_obc_path(to_bin_path);
      to_obc_path += fs::path::preferred_separator;
      to_obc_path += L"obc.exe";
      fs::remove(to_obc_path);

      fs::path to_obd_path(to_bin_path);
      to_obd_path += fs::path::preferred_separator;
      to_obd_path += L"obd.exe";
      fs::remove(to_obd_path);

      fs::path to_obb_path(to_bin_path);
      to_obb_path += fs::path::preferred_separator;
      to_obb_path += L"obb.exe";
      fs::remove(to_obb_path);
#else
      fs::path to_obc_path(to_bin_path);
      to_obc_path += fs::path::preferred_separator;
      to_obc_path += L"obc";
      fs::remove(to_obc_path);

      fs::path to_obd_path(to_bin_path);
      to_obd_path += fs::path::preferred_separator;
      to_obd_path += L"obd";
      fs::remove(to_obd_path);

      fs::path to_obb_path(to_bin_path);
      to_obb_path += fs::path::preferred_separator;
      to_obb_path += L"obb";
      fs::remove(to_obb_path);
#endif

      // copy 'lib' directory
      fs::path from_lib_path(runtime_base_dir);
      from_lib_path += fs::path::preferred_separator;
      from_lib_path += L"lib";

      fs::path to_lib_path(to_base_dir);      
      to_lib_path += fs::path::preferred_separator;
      to_lib_path += L"runtime";
      to_lib_path += fs::path::preferred_separator;
      to_lib_path += L"lib";
      
      fs::create_directory(to_lib_path);
      fs::copy(from_lib_path, to_lib_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);

      // delete unneeded Objeck library files 
      DeleteAllFileTypes(to_lib_path, L".obl");

      // copy launch executable and configuration file
      fs::path from_lib_misc_path_obn(runtime_base_dir);
      from_lib_misc_path_obn += fs::path::preferred_separator;
      from_lib_misc_path_obn += L"lib";
      from_lib_misc_path_obn += fs::path::preferred_separator;
      from_lib_misc_path_obn += L"native";
      from_lib_misc_path_obn += fs::path::preferred_separator;
      from_lib_misc_path_obn += L"misc";
      from_lib_misc_path_obn += fs::path::preferred_separator;
#ifdef _WIN32
      from_lib_misc_path_obn += L"obn.exe";
#else
      from_lib_misc_path_obn += L"obn";
#endif
      fs::copy(from_lib_misc_path_obn, to_base_dir);

      fs::path from_lib_misc_path_prop(runtime_base_dir);
      from_lib_misc_path_prop += fs::path::preferred_separator;
      from_lib_misc_path_prop += L"lib";
      from_lib_misc_path_prop += fs::path::preferred_separator;
      from_lib_misc_path_prop += L"native";
      from_lib_misc_path_prop += fs::path::preferred_separator;
      from_lib_misc_path_prop += L"misc";
      from_lib_misc_path_prop += fs::path::preferred_separator;
      from_lib_misc_path_prop += L"config.prop";
      fs::copy(from_lib_misc_path_prop, to_base_dir);

      // copy target application
      fs::path to_obe_file(to_app_path);
      to_obe_file += fs::path::preferred_separator;
      to_obe_file += L"app.obe";
      fs::copy(src_obe_path, to_obe_file);

      // copy auxiliary resource directory
      if(!src_dir_path.empty()) {
        fs::path to_obe_dir(to_app_path);
        to_obe_dir += fs::path::preferred_separator;
        to_obe_dir += "resources";
        fs::create_directory(to_obe_dir);

        fs::copy(src_dir_path, to_obe_dir);
      }

      // rename target binary
      fs::path from_exe_file(to_base_dir);
      from_exe_file += fs::path::preferred_separator;
      from_exe_file += L"obn";
#ifdef _WIN32
      from_exe_file += L".exe";
#endif
      
      fs::path to_exe_file(to_base_dir);
      to_exe_file += fs::path::preferred_separator;
      to_exe_file += to_name;
#ifdef _WIN32
      to_exe_file += L".exe";
#endif
      fs::rename(from_exe_file, to_exe_file);

      // we are done...
      wcout << L"Created native runtime environment for: '" + src_obe_file + L"' in directory '" + to_base_dir + L"'\n---" << endl;
    }
    catch(std::exception& e) {
      cerr << ">>> Error encounented build native runtime environment <<<" << endl;
      cerr << "\t" << e.what() << endl;
      exit(1);
    }
  }
  else {
    wcout << GetUsage() << endl;
    exit(1);
  }
  
  return 0;
}

wstring GetInstallDirectory() 
{
  wstring install_dir;

#ifdef _WIN32  
  char install_path[MAX_FILE_PATH];
  DWORD status = GetModuleFileNameA(nullptr, install_path, sizeof(install_path));
  if(status > 0) {
    string exe_path(install_path);
    size_t index = exe_path.find("\\app\\");
    if(index != string::npos) {
      install_dir = BytesToUnicode(exe_path.substr(0, index));
    }
  }
#else
  ssize_t status = 0;
  char install_path[MAX_FILE_PATH] = { 0 };
#ifdef _OSX
  uint32_t size = MAX_FILE_PATH;
  if(_NSGetExecutablePath(install_path, &size) != 0) {
    status = -1;
  }
#else
  status = ::readlink("/proc/self/exe", install_path, sizeof(install_path) - 1);
  if(status != -1) {
    install_path[status] = '\0';
  }
#endif
  if(status != -1) {
    string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('/');
    if(install_index != string::npos) {
      exe_path = exe_path.substr(0, install_index);
      install_index = exe_path.find_last_of('/');
      if(install_index != string::npos) {
        install_dir = BytesToUnicode(exe_path.substr(0, install_index));
      }
    }
  }
#endif

  return install_dir;
}

bool CheckInstallDir(const wstring& install_dir) 
{
  // sanity check
  fs::path readme_path(install_dir);
  readme_path += fs::path::preferred_separator;
  readme_path += L"readme.html";

  fs::path license_path(install_dir);
  license_path += fs::path::preferred_separator;
  license_path += L"LICENSE";

  return fs::exists(readme_path) && fs::exists(license_path);
}

map<const wstring, wstring> ParseCommnadLine(int argc, char* argv[], list<wstring> &options)
{
  map<const wstring, wstring> arguments;

  // reconstruct command line
  string path;
  for(int i = 1; i < 1024 && i < argc; ++i) {
    path += ' ';
    char* cmd_param = argv[i];
    if(strlen(cmd_param) > 0 && cmd_param[0] != L'\'' && (strrchr(cmd_param, L' ') || strrchr(cmd_param, L'\t'))) {
      path += '\'';
      path += cmd_param;
      path += '\'';
    }
    else {
      path += cmd_param;
    }
  }

  // get command line parameters
  wstring path_string = BytesToUnicode(path);

  size_t pos = 0;
  size_t end = path_string.size();
  while(pos < end) {
    // ignore leading white space
    while(pos < end && (path_string[pos] == L' ' || path_string[pos] == L'\t')) {
      pos++;
    }
    if(path_string[pos] == L'-' && pos > 0 && path_string[pos - 1] == L' ') {
      // parse key
      size_t start = ++pos;
      while(pos < end && path_string[pos] != L' ' && path_string[pos] != L'\t') {
        pos++;
      }
      const wstring key = path_string.substr(start, pos - start);
      // parse value
      while(pos < end && (path_string[pos] == L' ' || path_string[pos] == L'\t')) {
        pos++;
      }
      start = pos;
      bool is_string = false;
      if(pos < end && path_string[pos] == L'\'') {
        is_string = true;
        start++;
        pos++;
      }
      bool not_end = true;
      while(pos < end && not_end) {
        // check for end
        if(is_string) {
          not_end = path_string[pos] != L'\'';
        }
        else {
          not_end = !(path_string[pos] == L' ' || path_string[pos] == L'\t');
        }
        // update position
        if(not_end) {
          pos++;
        }
      }
      wstring value = path_string.substr(start, pos - start);

      // close string and add
      if(path_string[pos] == L'\'') {
        pos++;
      }

      map<const wstring, wstring>::iterator found = arguments.find(key);
      if(found != arguments.end()) {
        value += L',';
        value += found->second;
      }
      arguments[key] = value;
    }
    else {
      pos++;
    }
  }

  for(map<const wstring, wstring>::iterator intr = arguments.begin(); intr != arguments.end(); ++intr) {
    options.push_back(intr->first);
  }

  return arguments;
}

wstring GetCommandParameter(const wstring& key, map<const wstring, wstring>& cmd_params, list<wstring>& argument_options, bool optional)
{
  wstring value;
  map<const wstring, wstring>::iterator result = cmd_params.find(key);
  if(result != cmd_params.end()) {
    value = result->second;
    argument_options.remove(key);
  }
  else if(false) {
    argument_options.remove(key);
  }

  TrimFileEnding(value);
  return value;
}

bool EndsWith(const wstring& str, const wstring& ending)
{
  if(str.length() >= ending.length()) {
    return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
  }

  return false;
}

void DeleteAllFileTypes(const fs::path& from_dir, const fs::path ext_type)
{
  try {
    for(const auto& inter : fs::directory_iterator(from_dir)) {
      if(inter.path().extension() == ext_type) {
        fs::remove(inter.path());
      }
    }
  }
  catch(std::exception& e) {
    throw e;
  }
}

void TrimFileEnding(wstring& filename)
{
  if(!filename.empty() && filename.back() == fs::path::preferred_separator) {
    filename.pop_back();
  }
}

wstring GetUsage()
{
  wstring usage;

  usage += L"Usage: obb -src <input *.obe file> -to_dir <output directory> -to_name <name of target exe> -install <root Objeck directory>\n\n";
  usage += L"Options:\n";
  usage += L"  -src_file: [input] source .obe file\n";
  usage += L"  -src_dir:  [optional] directory of content to copy to app/resources\n";
  usage += L"  -to_dir:   [output] output file directory\n";
  usage += L"  -to_name:  [output] output app name\n";
  usage += L"  -install:  [optional] root Objeck directory to copy the runtime from\n";
  usage += L"\nExample: \"obb -src /tmp/hello.obe -to_dir /tmp -to_name hello -install /opt/objeck-lang\"\n\nVersion: ";
  usage += VERSION_STRING;

  return usage;
}