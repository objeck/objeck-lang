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
  map<const wstring, wstring> cmd_params = ParseCommnadLine(argc, argv);

  list<wstring> argument_options;
  for(map<const wstring, wstring>::iterator intr = cmd_params.begin(); intr != cmd_params.end(); ++intr) {
    argument_options.push_back(intr->first);
  }

  // check program target
  wstring runtime_base_dir;
  map<const wstring, wstring>::iterator result = cmd_params.find(L"install");
  if(result != cmd_params.end()) {
    runtime_base_dir = result->second;
    argument_options.remove(L"install");
  }
  else {
    runtime_base_dir = GetInstallDirectory();
    argument_options.remove(L"install");
  }

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
  result = cmd_params.find(L"src");
  if(result != cmd_params.end()) {
    src_obe_file = result->second;
    if(!EndsWith(src_obe_file, L".obe")) {
      wcout << GetUsage() << endl;
      exit(1);
    }
    argument_options.remove(L"src");
  }

  if(argument_options.empty()) {
    to_base_dir += fs::path::preferred_separator;
    to_base_dir += to_name;

    try {
      bool is_ok = true;

      if(!fs::exists(src_obe_file)) {
        is_ok = false;
        wcerr << ">>> Unable to find source file '" << src_obe_file << L"' <<<" << endl;
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
      
      fs::copy(from_bin_path, to_bin_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);

#ifdef _WIN32
      fs::remove(to_bin_str + fs::path::preferred_separator + L"obc.exe");

      fs::remove(to_bin_str + fs::path::preferred_separator + L"obd.exe");
      fs::remove(to_bin_str + fs::path::preferred_separator + L"obb.exe");
#else
      fs::path renove(to_bin_str + fs::path::preferred_separator + L"obc");
      fs::path renove(to_bin_str + fs::path::preferred_separator + L"obd");
      fs::path renove(to_bin_str + fs::path::preferred_separator + L"obb");
#endif

      // copy 'lib'
      fs::path from_lib_path(runtime_base_dir + fs::path::preferred_separator + L"lib");
      fs::path to_lib_path(to_base_dir + fs::path::preferred_separator + L"runtime" + fs::path::preferred_separator + L"lib");
      fs::copy(from_lib_path, to_lib_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
      remove_all_file_types(to_lib_path, L".obl");

      // copy executable and configuration
      fs::path from_lib_misc_path_obn(runtime_base_dir + fs::path::preferred_separator + L"lib" +
                                      fs::path::preferred_separator + L"native" +
                                      fs::path::preferred_separator + L"misc" +
#ifdef _WIN32
                                      fs::path::preferred_separator + L"obn.exe");
#else
        fs::path::preferred_separator + L"obn");
#endif
      fs::copy(from_lib_misc_path_obn, to_base_dir);

      fs::path from_lib_misc_path_prop(runtime_base_dir + fs::path::preferred_separator + L"lib" +
                                       fs::path::preferred_separator + L"native" +
                                       fs::path::preferred_separator + L"misc" +
                                       fs::path::preferred_separator + L"config.prop");
      fs::copy(from_lib_misc_path_prop, to_base_dir);

      // copy app
      fs::path to_obe_file(to_app_path);
      to_obe_file += fs::path::preferred_separator;
      to_obe_file += L"app.obe";
      fs::copy(src_obe_file, to_obe_file);

      // rename binary
      fs::path from_exe_file(to_base_dir);
      from_exe_file += fs::path::preferred_separator;
      from_exe_file += L"obn.exe";

      fs::path to_exe_file(to_base_dir);
      to_exe_file += fs::path::preferred_separator;
      to_exe_file += to_name + L".exe";

      fs::rename(from_exe_file, to_exe_file);

      wcout << L"Successfully created native runtime for: '" + src_obe_file + L"' in '" + to_base_dir + L"'\n---" << endl;
    }
    catch(std::exception& e) {
      cerr << e.what() << endl;
    }
  }
  else {
    wcout << GetUsage() << endl;
    exit(1);
  }
  
  return 0;
}
