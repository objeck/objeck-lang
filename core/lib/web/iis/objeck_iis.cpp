#define _WINSOCKAPI_

#include "objeck_iis.h"

//
// IIS server module
//
ObjeckIIS::ObjeckIIS() {
  intpr = nullptr;
  op_stack = nullptr;
  stack_pos = nullptr;

  std::map<std::string, std::string> key_values = LoadConfiguration();
  const std::string progam_path = key_values["program_path"];
  std::string install_path = key_values["install_path"];
  std::string lib_name = key_values["lib_name"];

#ifdef _DEBUG
  const std::string debug_path = install_path + "\\iis_debug.txt";
  OpenLogger(debug_path);

  tmp_cout = std::wcout.rdbuf();
  std::wcout.rdbuf(GetLogger().rdbuf());

  DebugEnvironment(progam_path, install_path, lib_name);
#endif
  
  // TODO: check for end '\' and add in '\lib\' Windows-only coding
  SetEnvironmentVariable("OBJECK_LIB_PATH", install_path.c_str());

#ifdef _DEBUG
  GetLogger() << "Loading program: '" << progam_path.c_str() << "'" << std::endl;
#endif
  // load program
  loader = new Loader(BytesToUnicode(progam_path).c_str());
  loader->Load();

  // ignore non-web applications
  if(!loader->IsWeb()) {
    GetLogger() << L">>> Please recompile the code to be a web application <<<" << std::endl;
    exit(1);
  }
#ifdef _DEBUG
  GetLogger() << "Program loaded and checked" << std::endl;
#endif

  StartInterpreter();
}

ObjeckIIS::~ObjeckIIS() {
  std::wcout.rdbuf(tmp_cout);

  delete stack_pos;
  stack_pos = nullptr;

  delete[] op_stack;
  op_stack = nullptr;

  delete loader;
  intpr = nullptr;

  delete intpr;
  loader = nullptr;

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
        const std::string install_path = buffer_str.substr(0, find_pos);
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

void ObjeckIIS::StartInterpreter()
{
  // get execution method
  method = loader->GetStartMethod();
  if(!method) {
    GetLogger() << L">>> Unable to locate the 'Action(...)' function. <<<" << std::endl;
    exit(1);
  }

#ifdef _DEBUG
  GetLogger() << "Load method: '" << UnicodeToBytes(method->GetName()).c_str() << "'" << std::endl;
#endif

  intpr = new Runtime::StackInterpreter(Loader::GetProgram());
  Runtime::StackInterpreter::AddThread(intpr);

  op_stack = new size_t[OP_STACK_SIZE];
  stack_pos = new long;

#ifdef _DEBUG
  GetLogger() << "Initialized interpreter" << std::endl;
#endif
}

void ObjeckIIS::StopInterpreter()
{
  // clean up
  delete[] op_stack;
  op_stack = nullptr;

  delete stack_pos;
  stack_pos = nullptr;

  Runtime::StackInterpreter::RemoveThread(intpr);
  Runtime::StackInterpreter::HaltAll();

  delete intpr;
  intpr = nullptr;
}

REQUEST_NOTIFICATION_STATUS ObjeckIIS::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
{
  UNREFERENCED_PARAMETER(pProvider);

  // Retrieve a pointer to the response.
  IHttpRequest* request = pHttpContext->GetRequest();
  IHttpResponse* response = pHttpContext->GetResponse();

  // Test for an error.
  if(intpr && request && response) {
#ifdef _DEBUG
    GetLogger() << "Starting..." << std::endl;
#endif
    // execute method
    (*stack_pos) = 0;

#ifdef _DEBUG
    GetLogger() << "--- 0 ---" << std::endl;
#endif

    // create request and response
    size_t* req_obj = MemoryManager::AllocateObject(L"Web.Server.Request", op_stack, *stack_pos, false);
    size_t* res_obj = MemoryManager::AllocateObject(L"Web.Server.Response", op_stack, *stack_pos, false);

#ifdef _DEBUG
    GetLogger() << "--- 4 ---: request=" << req_obj << ", response=" << res_obj << std::endl;
#endif
/*    
    


    if(req_obj && res_obj) {
      req_obj[0] = (size_t)request;
      res_obj[0] = (size_t)response;

      // set method calling parameters
      op_stack[0] = (size_t)req_obj;
      op_stack[1] = (size_t)response;
      *stack_pos = 2;

      // execute method
      response->Clear();
      intpr->Execute(op_stack, stack_pos, 0, mthd, nullptr, false);
    }



    // End additional processing.
    return RQ_NOTIFICATION_FINISH_REQUEST;
*/

#ifdef _DEBUG
    GetLogger() << "Finishing..." << std::endl;
#endif
  }
  else {
    SetContentType("text/plain", response);
    WriteResponseString("Unable to service request", response);
    return RQ_NOTIFICATION_FINISH_REQUEST;
  }

  SetContentType("text/plain", response);
  WriteResponseString("Inching forward..", response);
  return RQ_NOTIFICATION_FINISH_REQUEST;

  // Return processing to the pipeline.
  // return RQ_NOTIFICATION_CONTINUE;
}

void ObjeckIIS::SetContentType(const std::string header, IHttpResponse* response)
{
  response->SetHeader(HttpHeaderContentType, header.c_str(), (USHORT)header.size(), TRUE);
}

bool ObjeckIIS::WriteResponseString(const std::string data, IHttpResponse* response)
{
  // create check
  HTTP_DATA_CHUNK data_chunk;
  data_chunk.DataChunkType = HttpDataChunkFromMemory;
  data_chunk.FromMemory.pBuffer = (PVOID)data.c_str();
  data_chunk.FromMemory.BufferLength = (USHORT)data.size();

  // write response
  DWORD sent;
  const HRESULT result = response->WriteEntityChunks(&data_chunk, 1, FALSE, TRUE, &sent);
  if(result != S_OK) {
    response->SetStatus(500, "Server Error", 0, result);
    return false;
  }

  return true;
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