/***************************************************************************
 * FastCGI support for Objeck
 *
 * Copyright (c) 2011-2015, Randy Hollines
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
 * - Neither the name of the Objeck Team nor the names of its
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

#include <stdlib.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#pragma comment(lib, "Rpcrt4.lib")
#else
#include <unistd.h>
#include <uuid/uuid.h>
extern char ** environ;
#endif

#include "fcgio.h"
#include "fcgi_config.h"
#include "../../vm/lib_api.h"

using namespace std;

#define UUID_LEN 36

string CreateUUID();
void fcgi_get_env_value(const char* name, VMContext& context);

extern "C" {
  //
  // initialize fcgi environment
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib() {
  }

  //
  // release fcgi resources
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_write(VMContext& context) {
    FCGX_Stream* out = (FCGX_Stream*)APITools_GetIntValue(context, 0);
    const wchar_t* value = APITools_GetStringValue(context, 1);
    if(out && value) {
      FCGX_PutS(UnicodeToBytes(value).c_str(), out);
    }
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_protocol(VMContext& context) {
    fcgi_get_env_value("SERVER_PROTOCOL", context);
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_query(VMContext& context) {
    fcgi_get_env_value("QUERY_STRING", context);
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_cookie(VMContext& context) {
    fcgi_get_env_value("HTTP_COOKIE", context);
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_remote_address(VMContext& context) {
    fcgi_get_env_value("REMOTE_ADDR", context);
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_request_method(VMContext& context) {
    fcgi_get_env_value("REQUEST_METHOD", context);
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_request_uri(VMContext& context) {
    fcgi_get_env_value("REQUEST_URI", context);
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_response(VMContext& context) {
    FCGX_Stream* in = (FCGX_Stream*)APITools_GetIntValue(context, 0);
    FCGX_ParamArray envp = (FCGX_ParamArray)APITools_GetIntValue(context, 1);

    if(in && environ) {
      char* buff_size_str = FCGX_GetParam("CONTENT_LENGTH", envp);
      if(buff_size_str) {
        long buff_size = atoi(buff_size_str);
        if(buff_size > 0 && buff_size < 1024 * 8) {
          char* buffer = new char[buff_size + 1];
          long read = FCGX_GetStr(buffer, buff_size, in);
          buffer[read] = '\0';
          APITools_SetStringValue(context, 2, BytesToUnicode(buffer));
          delete[] buffer;
          return;
        }
      }
    }

    APITools_SetStringValue(context, 1, L"");
  }

  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void fcgi_get_uuid(VMContext& context) {
    const string uuid = CreateUUID();
    APITools_SetStringValue(context, 1, BytesToUnicode(uuid));
  }
}

//
// TOOD
//
void fcgi_get_env_value(const char* name, VMContext& context) {
  FCGX_ParamArray envp = (FCGX_ParamArray)APITools_GetIntValue(context, 0);
  if(envp) {
    const char* value = FCGX_GetParam(name, envp);
    if(value) {
      APITools_SetStringValue(context, 1, BytesToUnicode(value));
      return;
    }
  }

  APITools_SetStringValue(context, 1, L"");
}

//
// TOOD
//
string CreateUUID() {
  uuid_t uuid;
  string uuid_str;

#ifdef _WIN32
  RPC_CSTR buffer = NULL;
  CoCreateGuid(&uuid);
  UuidToString(&uuid, &buffer);
  for(int i = 0; i < UUID_LEN; ++i) {
    if(buffer[i] != '-') {
      uuid_str += buffer[i];
    }
  }
  RpcStringFree(&buffer);
#else
  char buffer[UUID_LEN];
  bzero(buffer, UUID_LEN);
  uuid_generate(uuid);
  uuid_unparse(uuid, buffer);
  for(int i = 0; i < UUID_LEN; ++i) {
    if(buffer[i] != '-') {
      uuid_str += buffer[i];
    }
  }
#endif

  return uuid_str;
}


