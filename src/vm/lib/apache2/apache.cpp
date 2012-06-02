/***************************************************************************
 * ODBC support for Objeck
 *
 * Copyright (c) 2011-2012, Randy Hollines
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

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "http_log.h"

#include "../../lib_api.h"

using namespace std;

extern "C" {
  //
  // initialize odbc environment
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void load_lib() {
  }
  
  //
  // release odbc resources
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
    request_rec* request = (request_rec*)APITools_GetIntValue(context, 0);
    if(request) {
      const char* value = APITools_GetStringValue(context, 1);
      ap_rputs(value, request);
    }
  }
  
  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void fcgi_get_uri(VMContext& context) {
    request_rec* request = (request_rec*)APITools_GetIntValue(context, 0);
    if(request) {
      APITools_SetStringValue(context, 1, request->uri);
    }
    else {
      APITools_SetStringValue(context, 1, "");
    }
  }
  
  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void fcgi_get_args(VMContext& context) {
    request_rec* request = (request_rec*)APITools_GetIntValue(context, 0);
    if(request && request->args) {
      APITools_SetStringValue(context, 1, request->args);
      return;
    }
    
    APITools_SetStringValue(context, 1, "");
  }
  
  //
  // TOOD
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void fcgi_get_protocol(VMContext& context) {
    request_rec* request = (request_rec*)APITools_GetIntValue(context, 0);
    if(request && request->args) {
      APITools_SetStringValue(context, 1, request->protocol);
      return;
    }
    
    APITools_SetStringValue(context, 1, "");
  }
}
