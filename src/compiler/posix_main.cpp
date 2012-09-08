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
#include "../shared/version.h"
#include <iostream>
#include <string>
#include <list>
#include <map>

using namespace std;

/****************************
 * Program start. Parses command
 * line parameters.
 ****************************/
int main(int argc, char* argv[])
{
  string usage;
  usage += "Copyright (c) 2008-2012, Randy Hollines. All rights reserved.\n";
  usage += "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. REFER TO THE\n";
  usage += "license.txt file or http://www.opensource.org/licenses/bsd-license.php\n";
  usage += "FOR MORE INFORMATION.\n\n";
  usage += VERSION_STRING;
  usage += "\n\n";
  usage += "usage: obc -src <program [(',' program)...]> [-opt (s0|s1|s2|s3)] [-lib libary [(libary ',')...]] [-tar (exe|lib)] -dest <output>\n";
  usage += "example: \"obc -src ..\\examples\\hello.obs -dest hello.obe\"\n\n";
  usage += "options:\n";
  usage += "  -src: input source files (separated by ',')\n";
  usage += "  -opt: source optimizations (s0-s3 being the most aggressive) default is s0\n";
  usage += "  -lib: input linked libraries (separated by ',')\n";
  usage += "  -tar: output target ('lib' for linked library or 'exe' for executable) default is 'exe'\n";
  usage += "  -dest: output file name\n";
  usage += "  -debug: compile with debug symbols (must be last argument)";

  int status;
  if(argc >= 3) {
    // reconstruct path max of 32 arguments allowed
    string path;
    for(int i = 1; i < 32 && i < argc; i++) {
      path += " ";
      char* cmd_param = argv[i];
      if(strlen(cmd_param) > 0 && cmd_param[0]  != '\'' && 
	 (strrchr(cmd_param, ' ') || strrchr(cmd_param, '\t'))) {
	path += "'";
	path += cmd_param;
	path += "'";
      }
      else {
	path += cmd_param;
      }
    }
    // parse path
    int end = (int)path.size();
    map<const string, string> arguments;
    list<string> argument_options;
    int pos = 0;
    while(pos < end) {
      // ignore leading white space
      while( pos < end && (path[pos] == ' ' || path[pos] == '\t')) {
	pos++;
      }
      if(path[pos] == '-') {
	// parse key
	int start =  ++pos;
	while( pos < end && path[pos] != ' ' && path[pos] != '\t') {
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
	  start++;
	  pos++;
	}
	bool not_end = true;
	while(pos < end && not_end) {
	  // check for end
	  if(is_string) {
	    not_end = path[pos] != '\'';
	  }
	  else {
	    not_end = path[pos] != ' ' && path[pos] != '\t';
	  }
	  // update position
	  if(not_end) {
	    pos++;
	  }
	}
	string value = path.substr(start, pos - start);
	argument_options.push_back(key);
	arguments.insert(pair<string, string>(key, value));
      } 
      else {
	while(pos < end && (path[pos] == ' ' || path[pos] == '\t')) {
	  pos++;
	}
	int start = pos;
	while(pos < end && path[pos] != ' ' && path[pos] != '\t') {
	  pos++;
	}
	string value = path.substr(start, pos - start);
	arguments.insert(pair<string, string>("-", value));
      }
    }
    // compile source
    status = Compile(arguments, argument_options, usage);
  } 
  else {
    cerr << usage << endl << endl;
    status = USAGE_ERROR;
  }
  
  return status;
}
