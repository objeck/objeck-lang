#define _WINSOCKAPI_
#include "objeck_iis.h"

//
// IIS server module
//
ObjeckIIS::ObjeckIIS() {
  intpr = nullptr;

  std::map<std::string, std::string> key_values = LoadConfiguration();
  const std::string progam_path = key_values["program_path"]; 
  std::string lib_install_path = key_values["lib_path"];
  std::string lib_name = key_values["lib_name"];
#ifdef _DEBUG
  const std::string debug_path = lib_install_path + "\\iis_debug.txt";
  OpenLogger(debug_path);
  DebugEnvironment(progam_path, lib_install_path, lib_name);
#endif
  
  // TODO: check for end '\' and add in '\lib\' Windows-only coding
  SetEnvironmentVariable("OBJECK_LIB_PATH", lib_install_path.c_str());

  // load program
  Loader loader(BytesToUnicode(progam_path).c_str());
  loader.Load();

  // ignore non-web applications
  if(!loader.IsWeb()) {
    GetLogger() << L"Please recompile the code to be a web application." << std::endl;
    exit(1);
  }
#ifdef _DEBUG
  GetLogger() << "Loaded program: '" << progam_path.c_str() << "'" << std::endl;
#endif

  intpr = new Runtime::StackInterpreter(Loader::GetProgram());
  Runtime::StackInterpreter::AddThread(intpr);

  op_stack = new size_t[OP_STACK_SIZE];
  stack_pos = new long;

}

ObjeckIIS::~ObjeckIIS() {
  delete stack_pos;
  stack_pos = nullptr;

  delete[] op_stack;
  op_stack = nullptr;

  delete intpr;
  intpr = nullptr;

  CloseLogger();
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
        install_path = buffer_str.substr(0, find_pos);
        const std::string config_file_path = install_path + "\\config.ini";

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

REQUEST_NOTIFICATION_STATUS ObjeckIIS::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
{
  UNREFERENCED_PARAMETER(pProvider);

  // Retrieve a pointer to the response.
  IHttpRequest* request = pHttpContext->GetRequest();
  IHttpResponse* response = pHttpContext->GetResponse();

  // Test for an error.
  if(intpr && request && response) {
    // Clear the existing response.
    response->Clear();
      
    // Set the MIME type to plain text.
    const std::string header = "text/plain";
    response->SetHeader(HttpHeaderContentType, header.c_str(), (USHORT)header.size(), TRUE);

    const std::string html_out = "Hello World, Getting Things Ready...";

    // Create and set the data chunk.
    HTTP_DATA_CHUNK data_chunk;
    data_chunk.DataChunkType = HttpDataChunkFromMemory;
    data_chunk.FromMemory.pBuffer = (PVOID)html_out.c_str();
    data_chunk.FromMemory.BufferLength = (USHORT)html_out.size();

    // Insert the data chunk into the response.
    DWORD sent;
    const HRESULT result = response->WriteEntityChunks(&data_chunk, 1, FALSE, TRUE, &sent);
    if(result != S_OK) {
      // Set the HTTP status.
      response->SetStatus(500, "Server Error", 0, result);
    }

    // End additional processing.
    return RQ_NOTIFICATION_FINISH_REQUEST;
  }

  // Return processing to the pipeline.
  return RQ_NOTIFICATION_CONTINUE;
}

void ObjeckIIS::DebugEnvironment(const std::string& progam_path, const std::string& install_path, const std::string& lib_name) {
  GetLogger() << "Progam path='" << progam_path.c_str() << "'" << std::endl;
  GetLogger() << "Library path='" << install_path.c_str() << "'" << std::endl;
  GetLogger() << "Library name='" << lib_name.c_str() << "'" << std::endl;
  GetLogger() << "---" << std::endl;
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

// Create the module's exported registration function.
HRESULT __stdcall RegisterModule(DWORD dwServerVersion, IHttpModuleRegistrationInfo* pModuleInfo, IHttpServer* pGlobalInfo)
{
  UNREFERENCED_PARAMETER(dwServerVersion);
  UNREFERENCED_PARAMETER(pGlobalInfo);

  // Set the request notifications and exit.
  return pModuleInfo->SetRequestNotifications(new ObjeckIISFactory, RQ_BEGIN_REQUEST, 0);
}
// </Snippet1>