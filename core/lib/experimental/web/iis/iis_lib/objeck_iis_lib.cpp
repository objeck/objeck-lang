/***************************************************************************
 * IIS ISAPI client module
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

#define _WINSOCKAPI_

#include <windows.h>
#include <sal.h>
#include <httpserv.h>
#include <string>
#include "../../../vm/lib_api.h"
#include "../../../shared/sys.h"

#pragma comment(lib, "Ws2_32.lib")

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib() {
  }

  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
  }

  //
  // IIS response functions
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_set_content_type(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 0);
    const std::string header = UnicodeToBytes(APITools_GetStringValue(context, 1));

    response->SetHeader(HttpHeaderContentType, header.c_str(), (USHORT)header.size(), TRUE);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_append_string(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 1);
    const std::string data = UnicodeToBytes(APITools_GetStringValue(context, 2));

    // create check
    HTTP_DATA_CHUNK data_chunk;
    data_chunk.DataChunkType = HttpDataChunkFromMemory;
    data_chunk.FromMemory.pBuffer = (PVOID)data.c_str();
    data_chunk.FromMemory.BufferLength = (USHORT)data.size();

    // write response
    DWORD bytes_sent = 0;
    const HRESULT result = response->WriteEntityChunks(&data_chunk, 1, FALSE, TRUE, &bytes_sent);
    if(result != S_OK) {
      response->SetStatus(500, "Server Error in Objeck Module", 0, result);
    }

    APITools_SetIntValue(context, 0, bytes_sent);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_append_bytes(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 1);
    size_t* byte_array = APITools_GetObjectValue(context, 2);
    unsigned char* bytes = APITools_GetByteArray(byte_array);

    // create check
    HTTP_DATA_CHUNK data_chunk;
    data_chunk.DataChunkType = HttpDataChunkFromMemory;
    data_chunk.FromMemory.pBuffer = (PVOID)bytes;
    data_chunk.FromMemory.BufferLength = (USHORT)sizeof(bytes);

    // write response
    DWORD bytes_sent;
    const HRESULT result = response->WriteEntityChunks(&data_chunk, 1, FALSE, TRUE, &bytes_sent);
    if(result != S_OK) {
      response->SetStatus(500, "Server Error in Objeck Module", 0, result);
    }

    APITools_SetIntValue(context, 0, bytes_sent);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_remove_header(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 1);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 2));
    
    APITools_SetIntValue(context, 0, response->DeleteHeader(name.c_str()) == S_OK);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_set_header(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 1);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 2));
    const std::string value = UnicodeToBytes(APITools_GetStringValue(context, 3));

    APITools_SetIntValue(context, 0, response->SetHeader(name.c_str(), value.c_str(), (USHORT)value.size(), TRUE) == S_OK);
  }

  //
  // IIS request functions
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_request_remote_address(VMContext& context) {
    IHttpRequest* request = (IHttpRequest*)APITools_GetIntValue(context, 1);
    PSOCKADDR sock_addr = request->GetRemoteAddress();

    if(sock_addr != nullptr) {
      char buffer[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, (PSOCKADDR_IN)sock_addr, buffer, INET_ADDRSTRLEN);
      APITools_SetStringValue(context, 0, BytesToUnicode(buffer));
    }
    else {
      APITools_SetStringValue(context, 0, L"");
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_request_local_address(VMContext& context) {
    IHttpRequest* request = (IHttpRequest*)APITools_GetIntValue(context, 1);
    PSOCKADDR sock_addr = request->GetLocalAddress();

    if(sock_addr != nullptr) {
      char buffer[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, (PSOCKADDR_IN)sock_addr, buffer, INET_ADDRSTRLEN);
      APITools_SetStringValue(context, 0, BytesToUnicode(buffer));
    }
    else {
      APITools_SetStringValue(context, 0, L"");
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_request_get_header(VMContext& context) {
    IHttpRequest* request = (IHttpRequest*)APITools_GetIntValue(context, 1);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 2));

    USHORT value_length;
    PCSTR value = request->GetHeader(name.c_str(), &value_length);

    if(value_length > 0) {
      APITools_SetStringValue(context, 0, BytesToUnicode(value));
    }
    else {
      APITools_SetStringValue(context, 0, L"");
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_request_get_method(VMContext& context) {
    IHttpRequest* request = (IHttpRequest*)APITools_GetIntValue(context, 1);
    APITools_SetStringValue(context, 0, BytesToUnicode(request->GetHttpMethod()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_request_request_url(VMContext& context) {
    IHttpRequest* request = (IHttpRequest*)APITools_GetIntValue(context, 1);
    APITools_SetStringValue(context, 0, request->GetRawHttpRequest()->CookedUrl.pAbsPath);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_request_read_body(VMContext& context) {
    IHttpRequest* request = (IHttpRequest*)APITools_GetIntValue(context, 1);
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 2);

    std::vector<unsigned char> read_body;

    // read bytes and update buffer
    DWORD buffer_size = LARGE_BUFFER_MAX;
    unsigned char buffer[LARGE_BUFFER_MAX] = {0};
    while(request->GetRemainingEntityBytes()) {
      DWORD bytes_read = 0;
      const HRESULT result = request->ReadEntityBody(buffer, buffer_size, FALSE, &bytes_read);
      if(result != S_OK && ((result & 0x0000ffff) != ERROR_HANDLE_EOF)) {
        response->SetStatus(500, "Server Error in Objeck Module", 0, result);
        APITools_SetObjectValue(context, 0, nullptr);
        return;
      }

      read_body.insert(read_body.end(), buffer, buffer + bytes_read);
    }

    // allocate byte array and copy data
    size_t* array = APITools_MakeByteArray(context, (long)read_body.size());
    unsigned char* byte_array = (unsigned char*)(array + 3);
    memcpy(byte_array, read_body.data(), read_body.size() * sizeof(unsigned char));

    // create 'ByteArrayHolder' holder
    size_t* byte_obj = context.alloc_obj(L"System.ByteArrayHolder", context.op_stack, *context.stack_pos, false);
    byte_obj[0] = (size_t)array;

    APITools_SetObjectValue(context, 0, byte_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_clear_all(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 0);
    response->Clear();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void web_response_redirect(VMContext& context) {
    IHttpResponse* response = (IHttpResponse*)APITools_GetIntValue(context, 1);
    const std::string url = UnicodeToBytes(APITools_GetStringValue(context, 2));

    APITools_SetIntValue(context, 0, response->Redirect(url.c_str(), TRUE, TRUE) == S_OK);
  }
}