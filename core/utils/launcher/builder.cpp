/***************************************************************************
 * Builder for native execution environment
 *
 * Copyright (c) 2023, Randy Hollines
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
  
  wstring runtime_base_dir = GetCommandParameter(L"tool_dir", cmd_params, argument_options, true);
  wstring to_base_dir = GetCommandParameter(L"to_dir", cmd_params, argument_options);
  const wstring to_name = GetCommandParameter(L"to_name", cmd_params, argument_options);
  const wstring src_obe_file = GetCommandParameter(L"src_file", cmd_params, argument_options);
  const wstring src_dir = GetCommandParameter(L"src_dir", cmd_params, argument_options, true);

  if(runtime_base_dir.empty()) {
#ifdef _WIN32
    size_t path_len;
    char path_str[MAX_ENV_PATH];
    if(!getenv_s(&path_len, path_str, MAX_ENV_PATH, "OBJECK_LIB_PATH") && strlen(path_str) > 0) {
#else
    char* path_str = getenv("OBJECK_LIB_PATH");
    if(path_str) {
#endif
      runtime_base_dir = BytesToUnicode(path_str);
      runtime_base_dir += fs::path::preferred_separator;
      runtime_base_dir += L"..";
    }
  }
  
  // check command line parameters
  if(!EndsWith(src_obe_file, L".obe")) {
    wcout << GetUsage() << endl;
    exit(1);
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
        wcerr << ">>> Invalid Objeck toolchain directory: '" << runtime_base_dir << L"'" << endl; 
				wcout<<  "  Please add optional parameter `-tool_dir` or set environment variable `OBJECK_LIB_PATH` <<<" << endl;
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
      
      fs::copy(from_lib_path, to_lib_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);

      // delete unneeded Objeck library files 
      DeleteAllFileTypes(to_lib_path, L".obl");

      // copy launch executable and configuration file
      fs::path src_lib_misc_path_obn(runtime_base_dir);
      src_lib_misc_path_obn += fs::path::preferred_separator;
      src_lib_misc_path_obn += L"lib";
      src_lib_misc_path_obn += fs::path::preferred_separator;
      src_lib_misc_path_obn += L"native";
      src_lib_misc_path_obn += fs::path::preferred_separator;
      src_lib_misc_path_obn += L"misc";
      src_lib_misc_path_obn += fs::path::preferred_separator;

      fs::path dest_lib_misc_path_obn(to_base_dir);
      dest_lib_misc_path_obn += fs::path::preferred_separator;
      
#ifdef _WIN32
      src_lib_misc_path_obn += L"obn.exe";
      dest_lib_misc_path_obn += L"obn.exe";
#else
      src_lib_misc_path_obn += L"obn";
      dest_lib_misc_path_obn += L"obn";
#endif
      fs::copy(src_lib_misc_path_obn, dest_lib_misc_path_obn);      

      fs::path src_lib_misc_path_prop(runtime_base_dir);
      src_lib_misc_path_prop += fs::path::preferred_separator;
      src_lib_misc_path_prop += L"lib";
      src_lib_misc_path_prop += fs::path::preferred_separator;
      src_lib_misc_path_prop += L"native";
      src_lib_misc_path_prop += fs::path::preferred_separator;
      src_lib_misc_path_prop += L"misc";
      src_lib_misc_path_prop += fs::path::preferred_separator;
      src_lib_misc_path_prop += L"config.prop";

      fs::path dest_lib_misc_path_prop(to_base_dir);
      dest_lib_misc_path_prop += fs::path::preferred_separator;
      dest_lib_misc_path_prop += L"config.prop";
      fs::copy(src_lib_misc_path_prop, dest_lib_misc_path_prop);

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
      wcout << L"Created portable runtime for: '" + src_obe_file + L"' in directory '" + to_base_dir + L"'\n---" << endl;
    }
    catch(std::exception& e) {
      wcerr << ">>> Error encounented build native runtime environment <<<" << endl;
      wcerr << "\t" << e.what() << endl;
      exit(1);
    }
  }
  else {
    wcout << GetUsage() << endl;
    exit(1);
  }
  
  return 0;
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

/**
 * Launcher-specific command line parser wrapper
 * Uses the shared enhanced parser and populates the options list
 */
map<const wstring, wstring> ParseCommnadLine(int argc, const char* argv[], list<wstring> &options)
{
  // Use shared parser for enhanced GNU-style syntax support
  CommandLineParseResult result = ParseCommandLine(argc, argv);

  // Log any parsing errors
  for(const auto& error : result.errors) {
    wcerr << L"Warning: " << error << endl;
  }

  // Populate options list with all argument keys
  for(const auto& pair : result.arguments) {
    options.push_back(pair.first);
  }

  return result.arguments;
}

wstring GetCommandParameter(const wstring& key, map<const wstring, wstring>& cmd_params, list<wstring>& argument_options, bool optional)
{
  wstring value;
  map<const wstring, wstring>::iterator result = cmd_params.find(key);
  if(result != cmd_params.end()) {
    value = result->second;
    argument_options.remove(key);
  }
  else if(optional) {
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

static bool BytesToUnicode(const string& in, wstring& out)
{
#ifdef _WIN32
  // allocate space
  const int wsize = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, nullptr, 0);
  if(wsize == 0) {
    return false;
  }
  wchar_t* buffer = new wchar_t[wsize];

  // convert
  const int check = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, buffer, wsize);
  if(check == 0) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }

  // create string
  out.append(buffer, wsize - 1);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#else
  // allocate space
  size_t size = mbstowcs(nullptr, in.c_str(), in.size());
  if(size == (size_t)-1) {
    return false;
  }
  wchar_t* buffer = new wchar_t[size + 1];

  // convert
  size_t check = mbstowcs(buffer, in.c_str(), in.size());
  if(check == (size_t)-1) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  buffer[size] = L'\0';

  // create string
  out.append(buffer, size);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#endif

  return true;
}

wstring BytesToUnicode(const string& in)
{
  wstring out;
  if(BytesToUnicode(in, out)) {
    return out;
  }

  return L"";
}

wstring GetUsage()
{
  wstring usage = L"Usage: obb -src_file <input *.obe> -to_dir <target directory> -to_name <app name>\n\n";

  usage += L"Options:\n";
  usage += L"  -src_file: [input] source .obe file\n";
  usage += L"  -tool_dir: [optional] root Objeck tool chain directory to copy files from. Optional if the environment variable `OBJECK_LIB_PATH` is specified\n";
  usage += L"  -src_dir:  [optional] directory of content to copy to app/resources\n";
  usage += L"  -to_dir:   [output] output file directory\n";
  usage += L"  -to_name:  [output] output app name\n";
  usage += L"\nExample: \"obb -src_file /tmp/hello.obe -to_dir /tmp -to_name hello -tool_dir /opt/objeck-lang\"\n\nVersion: ";
  usage += VERSION_STRING;

  return usage;
}
