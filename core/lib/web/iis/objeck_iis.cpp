#define _WINSOCKAPI_

#include "objeck_iis.h"

//
// IIS module
//
ObjeckIIS::ObjeckIIS() {
  is_ok = false;
  intpr = nullptr;

  std::map<std::string, std::string> key_values = ObjeckIIS::LoadConfiguration();
  const std::string progam_path = key_values["progam_path"];
  const std::string install_path = key_values["install_path"];

  // load program
  Loader loader(BytesToUnicode(progam_path).c_str());
  loader.Load();
  html_out += "--- Progam loaded! ---";

  // ignore non-web applications
  if(!loader.IsWeb()) {
    std::wcout << L"Please recompile the code to be a web application." << std::endl;
    exit(1);
  }

  html_out += "\n--- Is Web... ---";


  intpr = new Runtime::StackInterpreter(Loader::GetProgram());
  Runtime::StackInterpreter::AddThread(intpr);

  op_stack = new size_t[OP_STACK_SIZE];
  stack_pos = new long;

  // TODO: continue...

  is_ok = true;
}

std::map<std::string, std::string> ObjeckIIS::LoadConfiguration()
{
  std::map<std::string, std::string> key_values;

  HMODULE module = GetModuleHandle("objeck_iis");
  if(module) {
    const size_t buffer_max = 512;
    char buffer[buffer_max] = {0};
    if(GetModuleFileName(module, (LPSTR)buffer, buffer_max)) {
      std::string buffer_str(buffer);
      size_t find_pos = buffer_str.find_last_of('\\');
      if(find_pos != std::wstring::npos) {
        std::string config_file_path = buffer_str.substr(0, find_pos);
        config_file_path += "\\config.ini";

        std::string line;
        std::ifstream file_in(config_file_path.c_str());
        if(file_in.good()) {
          std::getline(file_in, line);
          while(!file_in.eof()) {
            size_t index = line.find('=');
            if(index != std::string::npos) {
              const std::string key = line.substr(0, index);
              index++;
              const std::string value = line.substr(index, line.size() - index);
              key_values.insert(std::pair<std::string, std::string>(key, value));
            }
            // update
            std::getline(file_in, line);
          }
        }
      }
    }
  }

  return key_values;
}

ObjeckIIS::~ObjeckIIS() {
  if(is_ok) {
    delete stack_pos;
    stack_pos = nullptr;

    delete[] op_stack;
    op_stack = nullptr;

    delete intpr;
    intpr = nullptr;
  }
}

void ObjeckIIS::DebugEnvironment(const std::string &progam_path, const std::string & install_path) {
  html_out = "program=";
  html_out += progam_path;
  html_out += "\ninstall_path=";
  html_out += install_path;
  html_out += "\n";
}

void ObjeckIIS::LoadProgam(const std::wstring &name) {
  Loader loader(name.c_str());
  loader.Load();
}

REQUEST_NOTIFICATION_STATUS ObjeckIIS::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
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
    const std::string header = "text/plain";
    response->SetHeader(HttpHeaderContentType, header.c_str(), (USHORT)header.size(), TRUE);

    // Create and set the data chunk.
    HTTP_DATA_CHUNK data_chunk;
    data_chunk.DataChunkType = HttpDataChunkFromMemory;
    data_chunk.FromMemory.pBuffer = (PVOID)html_out.c_str();
    data_chunk.FromMemory.BufferLength = (USHORT)html_out.size();

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

//
// Module factory
//
HRESULT ObjeckIISFactory::GetHttpModule(OUT CHttpModule** ppModule, IN IModuleAllocator* pAllocator)
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

void ObjeckIISFactory::Terminate()
{
  // Remove the class from memory.
  delete this;
}

HRESULT RegisterModule(DWORD dwServerVersion, IHttpModuleRegistrationInfo* pModuleInfo, IHttpServer* pGlobalInfo)
{
  UNREFERENCED_PARAMETER(dwServerVersion);
  UNREFERENCED_PARAMETER(pGlobalInfo);

  // Set the request notifications and exit.
  return pModuleInfo->SetRequestNotifications(new ObjeckIISFactory, RQ_BEGIN_REQUEST, 0);
}