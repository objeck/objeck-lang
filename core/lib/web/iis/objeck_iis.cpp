#define _WINSOCKAPI_

#include "objeck_iis.h"

Loader* ObjeckIIS::loader = nullptr;
Runtime::StackInterpreter* ObjeckIIS::intpr = nullptr;
StackMethod* ObjeckIIS::method = nullptr;

size_t* ObjeckIIS::op_stack = nullptr;
long* ObjeckIIS::stack_pos = nullptr;

#ifdef _DEBUG
std::wstreambuf* ObjeckIIS::tmp_wcout;
std::wstreambuf* ObjeckIIS::tmp_werr;
#endif

//
// IIS server module
//
ObjeckIIS::ObjeckIIS() {
  if(!intpr) {
    std::map<std::string, std::string> key_values = LoadConfiguration();
    const std::string progam_path = key_values["program_path"];
    std::string install_path = key_values["install_path"];
    std::string lib_name = key_values["lib_name"];

#ifdef _DEBUG
    const std::string debug_path = install_path + "\\iis_debug.txt";
    OpenLogger(debug_path);

    tmp_wcout = std::wcout.rdbuf();
    std::wcout.rdbuf(GetLogger().rdbuf());

    tmp_werr = std::wcerr.rdbuf();
    std::wcerr.rdbuf(GetLogger().rdbuf());

    LogSetupEnvironment(progam_path, install_path, lib_name);
#endif

    // TODO: check for end '\' and add in '\lib\' Windows-only coding
    if(_putenv_s("OBJECK_LIB_PATH", install_path.c_str())) {
      GetLogger() << L">>> Unable to set OBJECK_LIB_PATH=" << install_path.c_str() << std::endl;
      exit(1);
    }
    
#ifdef _DEBUG
    GetLogger() << "--- Loading program: '" << progam_path.c_str() << "' ---" << std::endl;
#endif
    // load program
    loader = new Loader(BytesToUnicode(progam_path).c_str());
    loader->Load();

    // ignore non-web applications
    if(!loader->IsWeb()) {
      GetLogger() << L">>> Please recompile the code to be a web application <<<" << std::endl;
      exit(1);
    }

    loader->GetProgram()->SetProperty(L"OBJECK_LIB_WEB_SERVER", BytesToUnicode(lib_name));

#ifdef _DEBUG
    GetLogger() << "--- Program loaded and checked ---" << std::endl;
#endif

    StartInterpreter();
  }
}

ObjeckIIS::~ObjeckIIS() {
}

void ObjeckIIS::StopInterpreter()
{
#ifdef _DEBUG
  std::wcout.rdbuf(tmp_wcout);
  std::wcout.rdbuf(tmp_werr);
#endif
  
  Runtime::StackInterpreter::RemoveThread(intpr);
  Runtime::StackInterpreter::HaltAll();

  // clean up
  delete intpr;
  intpr = nullptr;

  delete[] op_stack;
  op_stack = nullptr;

  delete stack_pos;
  stack_pos = nullptr;

  delete loader;
  loader = nullptr;
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
          do {
            std::getline(file_in, line);
            size_t index = line.find('=');
            if(index != std::string::npos) {
              const std::string key = line.substr(0, index);
              index++;
              const std::string value = line.substr(index, line.size() - index);
              key_values.insert(std::pair<std::string, std::string>(key, value));
            }
          }
          while(!file_in.eof());
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
  GetLogger() << "--- Load method: '" << UnicodeToBytes(method->GetName()).c_str() << "' ---" << std::endl;
#endif

  intpr = new Runtime::StackInterpreter(Loader::GetProgram());
  Runtime::StackInterpreter::AddThread(intpr);

  op_stack = new size_t[OP_STACK_SIZE];
  stack_pos = new long;

#ifdef _DEBUG
  GetLogger() << "--- Initialized interpreter ---" << std::endl;
#endif
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
    GetLogger() << "--- Starting call... ---" << std::endl;
#endif
    // execute method
    (*stack_pos) = 0;

    // create request and response
    size_t* req_obj = MemoryManager::AllocateObject(L"Web.Server.Request", op_stack, *stack_pos, false);
    req_obj[0] = (size_t)request;

    size_t* res_obj = MemoryManager::AllocateObject(L"Web.Server.Response", op_stack, *stack_pos, false);
    res_obj[0] = (size_t)response;

    if(req_obj && res_obj) {
#ifdef _DEBUG
      GetLogger() << "--- Starting method call ---" << std::endl;
#endif
      req_obj[0] = (size_t)request;
      res_obj[0] = (size_t)response;

      // set method calling parameters
      op_stack[0] = (size_t)req_obj;
      op_stack[1] = (size_t)res_obj;
      *stack_pos = 2;

      // execute method
      response->Clear();
      intpr->Execute(op_stack, stack_pos, 0, method, nullptr, false);

#ifdef _DEBUG
      GetLogger() << "--- Ended method call: stack_pos" << *stack_pos << " --- " << std::endl;
#endif
    }
    else {
      return RQ_NOTIFICATION_CONTINUE;
    }
#ifdef _DEBUG
    GetLogger() << "--- Fin. ---" << std::endl;
#endif
  }
  else {
    SetContentType("text/html", response);
    WriteResponseString("<htm><b>Was unable to initialize enviroment...</b></html>", response);
    return RQ_NOTIFICATION_FINISH_REQUEST;
  }

  // end request
  return RQ_NOTIFICATION_FINISH_REQUEST;
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

void ObjeckIIS::LogSetupEnvironment(const std::string& progam_path, const std::string& install_path, const std::string& lib_name) {
  GetLogger() << "--- Progam path='" << progam_path.c_str() << "' ---" << std::endl;
  GetLogger() << "--- Library path='" << install_path.c_str() << "' ---" << std::endl;
  GetLogger() << "--- Library name='" << lib_name.c_str() << "' ---" << std::endl;
  GetLogger() << "===" << std::endl;
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
  ObjeckIIS::StopInterpreter();
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