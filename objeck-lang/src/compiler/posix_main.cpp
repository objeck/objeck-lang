/***************************************************************************
 * Starting point of the language compiler
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


#define USAGE_ERROR -1
#define SYSTEM_ERROR -2

#include "compiler.h"
#include <iostream>
#include <string>
#include <map>

using namespace std;

/****************************
 * Program start. Parses command
 * line parameters.
 ****************************/
int main(int argc, char* argv[])
{
  string usage;
  usage += "Copyright (c) 2008-2010, Randy Hollines. All rights reserved.\n";
  usage += "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
  usage += "license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
  usage += "FOR MORE INFORMATION.\n\n";
  usage += "usage: obc -src <program [(',' program)...]> [-opt (s0|s1|s2|s3)] [-lib libary [(libary ',')...]] [-tar (exe|lib)] -out <output>\n";
  usage += "example: \"obc -src test_src\\prgm1.obs -dest prgm1.obe\"\n\n";
  usage += "options:\n";
  usage += "  -src: input source files (separated by ',')\n";
  usage += "  -opt: source optimizations (s3 being the most aggressive) default is s0\n";
  usage += "  -lib: input linked libraries (separated by ',')\n";
  usage += "  -tar: output target (lib for linked library or exe for execute) default is exe\n";
  usage += "  -out: output file name";

  int status;
  if(argc >= 3) {
    // reconstruct path
    string path;
    for(int i = 1; i < argc; i++) {
      path += " ";
      path += argv[i];
    }

    // parse path
    int end = (int)path.size();
    map<const string, string> arguments;
    int pos = 0;
    while(pos < end) {
      // ignore leading white space
      while((path[pos] == ' ' || path[pos] == '\t') && pos < end) {
	pos++;
      }
      if(path[pos] == '-') {
	// parse key
	int start =  ++pos;
	while(path[pos] != ' ' && path[pos] != '\t' && pos < end) {
	  pos++;
	}
	string key = path.substr(start, pos - start);
	// parse value
	while(pos < end && (path[pos] == ' ' || path[pos] == '\t')) {
	  pos++;
	}
	start = pos;
  bool is_string = false;
  if(pos < end && path[pos] == '\'') {
    is_string = true;
    pos++;
  }
  bool not_end = true;
	while(pos < end && not_end) {
    if(is_string) {
      not_end = path[pos] != '\'';
    }
    else {
      not_end = path[pos] != ' ' && path[pos] != '\t';
    }
	  pos++;
	}
	string value;
  if(is_string) {
    value = path.substr(start + 1, pos - start - 2);
  }
  else {
    value = path.substr(start, pos - start - 1);
  }
	arguments.insert(pair<string, string>(key, value));
      } 
      else {
	while((path[pos] == ' ' || path[pos] == '\t') && pos < end) {
	  pos++;
	}
	int start = pos;
	while(path[pos] != ' ' && path[pos] != '\t' && pos < end) {
	  pos++;
	}
	string value = path.substr(start, pos - start);
	arguments.insert(pair<string, string>("-", value));
      }
    }
    // compile source
    status = Compile(arguments, usage);
  } 
  else {
    cerr << usage << endl << endl;
    status = USAGE_ERROR;
  }
  
  return status;
}
