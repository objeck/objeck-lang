/***************************************************************************
 * Native executable builder
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
  map<const wstring, wstring> cmd_params = ParseCommnadLine(argc, argv);
  if(cmd_params.size() < 3) {
    wcout << GetUsage() << endl;
    exit(1);
  }
  
  list<wstring> argument_options;
  for(map<const wstring, wstring>::iterator intr = cmd_params.begin(); intr != cmd_params.end(); ++intr) {
    argument_options.push_back(intr->first);
  }

  wstring runtime_base_dir;
  map<const wstring, wstring>::iterator result = cmd_params.find(L"install");
  if(result != cmd_params.end()) {
    runtime_base_dir = result->second;
  }
  else {
    runtime_base_dir = GetInstallDirectory();
  }
  argument_options.remove(L"install");

  wstring to_base_dir;
  result = cmd_params.find(L"to_dir");
  if(result != cmd_params.end()) {
    to_base_dir = result->second;
    argument_options.remove(L"to_dir");
  }

  wstring to_name;
  result = cmd_params.find(L"to_name");
  if(result != cmd_params.end()) {
    to_name = result->second;
    argument_options.remove(L"to_name");
  }

  wstring src_obe_file;
  result = cmd_params.find(L"src_file");
  if(result != cmd_params.end()) {
    src_obe_file = result->second;
    if(!EndsWith(src_obe_file, L".obe")) {
      wcout << GetUsage() << endl;
      exit(1);
    }
    argument_options.remove(L"src_file");
  }

  wstring src_dir;
  result = cmd_params.find(L"src_dir");
  if(result != cmd_params.end()) {
    src_dir = result->second;
  }
  argument_options.remove(L"src_dir");

  TrimFilename(runtime_base_dir);
  TrimFilename(to_base_dir);
  TrimFilename(to_name);
  TrimFilename(src_obe_file);
  TrimFilename(src_dir);

  // if parameters look good...
  if(argument_options.empty()) {
    to_base_dir += fs::path::preferred_separator;
    to_base_dir += to_name;

    try {
      bool is_ok = true;

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
      
      fs::create_directory(to_base_dir);
      fs::create_directory(to_base_dir);
      
      fs::path to_runtime_path(to_base_dir);
      to_runtime_path += fs::path::preferred_separator;
      to_runtime_path += L"runtime";
      fs::create_directory(to_runtime_path);

      fs::path to_app_path(to_base_dir);
      to_app_path += fs::path::preferred_separator;
      to_app_path += L"app";      
      fs::create_directory(to_app_path);

      // copy 'bin'
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

      // copy 'lib'
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
      remove_all_file_types(to_lib_path, L".obl");

      // copy executable and configuration
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

      // copy app
      fs::path to_obe_file(to_app_path);
      to_obe_file += fs::path::preferred_separator;
      to_obe_file += L"app.obe";
      fs::copy(src_obe_path, to_obe_file);

      // TODO: copy source directory
      if(!src_dir_path.empty()) {
        fs::path to_obe_dir(to_app_path);
        to_obe_dir += fs::path::preferred_separator;
        to_obe_dir += "resources";
        fs::create_directory(to_obe_dir);

        fs::copy(src_dir_path, to_obe_dir);
      }

      // rename binary
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

      wcout << L"Successfully created native runtime for: '" + src_obe_file + L"' in '" + to_base_dir + L"'\n---" << endl;
    }
    catch(std::exception& e) {
      cerr << e.what() << endl;
      exit(1);
    }
  }
  else {
    wcout << GetUsage() << endl;
    exit(1);
  }
  
  return 0;
}
