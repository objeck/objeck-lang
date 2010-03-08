/***************************************************************************
 * Program loader.
 *
 * Copyright (c) 2008-2009, Randy Hollines
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
 * - Neither the name of the StackVM Team nor the names of its
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

#ifndef __LOADER_H__
#define __LOADER_H__

#include "common.h"
#include <string.h>

using namespace std;

class Loader {
  int arg_count;
  vector<string> arguments;
  int num_char_strings;
  StackMethod* init_method;
  int string_cls_id;
  StackProgram* program;
  string filename;
  char* buffer;
  char* alloc_buffer;
  int buffer_size;
  int buffer_pos;
  int start_class_id;
  int start_method_id;

  int ReadInt() {
    int32_t value;
    memcpy(&value, buffer, sizeof(int32_t));
    buffer += sizeof(int32_t);
    return value;
  }

  string ReadString() {
    int size = ReadInt();
    string value(buffer, size);
    buffer += size;
    return value;
  }

  FLOAT_VALUE ReadDouble() {
    FLOAT_VALUE value;
    memcpy(&value, buffer, sizeof(FLOAT_VALUE));
    buffer += sizeof(FLOAT_VALUE);
    return value;
  }

  // loads a file into memory
  char* LoadFileBuffer(string filename, int &buffer_size) {
    char* buffer = NULL;
    // open file
    ifstream in(filename.c_str(), ifstream::binary);
    if(in) {
      // get file size
      in.seekg(0, ios::end);
      buffer_size = in.tellg();
      in.seekg(0, ios::beg);
      buffer = new char[buffer_size];
      in.read(buffer, buffer_size);
      // close file
      in.close();
    } else {
      cout << "Unable to open file: " << filename << endl;
      exit(1);
    }

    return buffer;
  }

  void ReadFile() {
    buffer_pos = 0;
    alloc_buffer = buffer = LoadFileBuffer(filename, buffer_size);
  }

  // loading functions
  void LoadEnums();
  void LoadClasses();
  void LoadMethods(StackClass* cls);
  void LoadInitializationCode(StackMethod* mthd);
  void LoadStatements(StackMethod* mthd);

public:
  Loader(const int argc, char** argv) {
    filename = argv[1];
    for(int i = 2; i < argc; i++) {
      arguments.push_back(argv[i]);
    }
    string_cls_id = -1;
    ReadFile();
    program = new StackProgram;
  }

  ~Loader() {
    if(alloc_buffer) {
      delete[] alloc_buffer;
      alloc_buffer = NULL;
    }

    delete program;
    program = NULL;
  }

  StackProgram* GetProgram() {
    return program;
  }

  void Load();
};

#endif
