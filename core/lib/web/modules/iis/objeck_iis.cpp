// <Snippet1>
#define _WINSOCKAPI_
#include <windows.h>
#include <sal.h>
#include <httpserv.h>

#include <iostream>
#include "sys.h"

// Create the module class.
class ObjeckIIS : public CHttpModule
{
public:
  std::string m_html;

  ObjeckIIS() {
    const size_t buffer_max = 513;
    wchar_t buffer[buffer_max];
    GetPrivateProfileString(L"objeck", L"program", L"(none)", (LPWSTR)&buffer, buffer_max, L"C:\\inetpub\\wwwroot\\config.ini");
    m_html = UnicodeToBytes(buffer);
  }

  ~ObjeckIIS() {

  }

  REQUEST_NOTIFICATION_STATUS OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
  {
    UNREFERENCED_PARAMETER(pProvider);

    // Retrieve a pointer to the response.
    IHttpRequest* request = pHttpContext->GetRequest();
    IHttpResponse* response = pHttpContext->GetResponse();

    // Test for an error.
    if(request && response) {
      // Clear the existing response.
      response->Clear();
      
      // Set the MIME type to plain text.
      const std::string header = "text/html";
      response->SetHeader(HttpHeaderContentType, header.c_str(), (USHORT)header.size(), TRUE);

      // Create and set the data chunk.
      HTTP_DATA_CHUNK data_chunk;
      data_chunk.DataChunkType = HttpDataChunkFromMemory;
      data_chunk.FromMemory.pBuffer = (PVOID)m_html.c_str();
      data_chunk.FromMemory.BufferLength = (USHORT)m_html.size();

      // Insert the data chunk into the response.
      DWORD sent;
      const HRESULT result = response->WriteEntityChunks(&data_chunk, 1, FALSE, TRUE, &sent);

      // Test for an error.
      if(FAILED(result)) {
        // Set the HTTP status.
        response->SetStatus(500, "Server Error", 0, result);
      }

      // End additional processing.
      return RQ_NOTIFICATION_FINISH_REQUEST;
    }

    // Return processing to the pipeline.
    return RQ_NOTIFICATION_CONTINUE;
  }
};

// Create the module's class factory.
class ObjeckIISFactory : public IHttpModuleFactory
{
public:
  HRESULT
    GetHttpModule(OUT CHttpModule** ppModule, IN IModuleAllocator* pAllocator)
  {
    UNREFERENCED_PARAMETER(pAllocator);

    // Create a new instance.
    ObjeckIIS* pModule = new ObjeckIIS;

    // Test for an error.
    if(!pModule) {
      // Return an error if the factory cannot create the instance.
      return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    }
    else {
      // Return a pointer to the module.
      *ppModule = pModule;
      pModule = nullptr;
      // Return a success status.
      return S_OK;
    }
  }

  void Terminate()
  {
    // Remove the class from memory.
    delete this;
  }
};

// Create the module's exported registration function.
HRESULT
__stdcall
RegisterModule(DWORD dwServerVersion, IHttpModuleRegistrationInfo* pModuleInfo, IHttpServer* pGlobalInfo)
{
  UNREFERENCED_PARAMETER(dwServerVersion);
  UNREFERENCED_PARAMETER(pGlobalInfo);

  // Set the request notifications and exit.
  return pModuleInfo->SetRequestNotifications(new ObjeckIISFactory, RQ_BEGIN_REQUEST, 0);
}
// </Snippet1>