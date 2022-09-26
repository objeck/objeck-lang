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
  if(argc == 3) {
    const wstring from_base_dir = BytesToUnicode(argv[1]);
    const wstring to_base_dir = BytesToUnicode(argv[2]) + fs::path::preferred_separator + L"app";

    try {
      if(!fs::exists(to_base_dir)) {
        fs::create_directory(to_base_dir);
        fs::create_directory(to_base_dir + fs::path::preferred_separator + L"app");
        fs::create_directory(to_base_dir + fs::path::preferred_separator + L"runtime");

        // copy 'bin'
        fs::path from_bin_path(from_base_dir + fs::path::preferred_separator + L"bin");

        const wstring to_bin_str = to_base_dir + fs::path::preferred_separator + L"runtime" + fs::path::preferred_separator + L"bin";
        fs::path to_bin_path(to_bin_str);

        fs::copy(from_bin_path, to_bin_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);

#ifdef _WIN32
        fs::remove(to_bin_str + fs::path::preferred_separator + L"obc.exe");
        fs::remove(to_bin_str + fs::path::preferred_separator + L"obd.exe");
#else
        fs::path renove(to_bin_str + fs::path::preferred_separator + L"obc");
        fs::path renove(to_bin_str + fs::path::preferred_separator + L"obd");
#endif

        // copy 'lib'
        fs::path from_lib_path(from_base_dir + fs::path::preferred_separator + L"lib");
        fs::path to_lib_path(to_base_dir + fs::path::preferred_separator + L"runtime" + fs::path::preferred_separator + L"lib");
        fs::copy(from_lib_path, to_lib_path, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
        remove_all_file_types(to_lib_path, L".obl");

        // TOOD: copy app launcher and prop files from 'lib/native/misc'
        // ...
      }
      else {
        cerr << ">>> File or directory already exists <<<" << endl;
        exit(1);
      }
    }
    catch(std::exception& e) {
      cerr << e.what() << endl;
    }
  }

  return 0;
}