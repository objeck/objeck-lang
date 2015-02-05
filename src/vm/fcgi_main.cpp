/***************************************************************************
 * Starting point for FastCGI module
 *
 * Copyright (c) 2012-2015 Randy Hollines
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

#include "fcgi_main.h"

/******************************
 * FCGI entry point
 ******************************/
int main(const int argc, const char* argv[])
{
#ifdef _DEBUG 
  Sleep(15 * 1000); // mainly for remote debugging in IIS
#endif

  wstring program_path;
  const char* raw_program_path = FCGX_GetParam("FCGI_CONFIG_PATH", environ);
  if(!raw_program_path) {
    wcerr << L"Unable to find program, please ensure the 'FCGI_CONFIG_PATH' variable has been set correctly." << endl;
    exit(1);
  }
  else {
    program_path = BytesToUnicode(raw_program_path);
  }
  
#ifdef _WIN32
  // enable Unicode console support
  _setmode(_fileno(stdin), _O_U16TEXT);
  _setmode(_fileno(stdout), _O_U16TEXT);

  // initialize Winsock
  WSADATA data;
  int version = MAKEWORD(2, 2);
  if(WSAStartup(version, &data)) {
    wcerr << L"Unable to load Winsock 2.2!" << endl;
    exit(1);
  }
#else
  // enable UTF-8 enviroment
  char* locale = setlocale(LC_ALL, ""); 
  std::locale lollocale(locale);
  setlocale(LC_ALL, locale); 
  std::wcout.imbue(lollocale);
#endif
  // Initialize OpenSSL
  CRYPTO_malloc_init();
  SSL_library_init();

  Loader loader(program_path.c_str());
  loader.Load();

  // ignore web applications
  if(!loader.IsWeb()) {
    wcerr << L"Please recompile the code to be a web application." << endl;
    exit(1);
  }

#ifdef _TIMING
  clock_t start = clock();
#endif

  // locate starting class and method
  StackMethod* mthd = loader.GetStartMethod();
  if(!mthd) {
    wcerr << L"Unable to locate the 'Request(args)' function." << endl;
    exit(1);
  }

#ifdef _DEBUG
  wcerr << L"### Loaded method: " << mthd->GetName() << L" ###" << endl;
#endif

  Runtime::StackInterpreter intpr(Loader::GetProgram());

  // go into accept loop...
  FCGX_Stream*in; FCGX_Stream* out; FCGX_Stream* err;
  FCGX_ParamArray envp;

  while(mthd && (FCGX_Accept(&in, &out, &err, &envp) >= 0)) {
    // execute method
    long* op_stack = new long[CALC_STACK_SIZE];
    long* stack_pos = new long;

    // create request and response
    long* req_obj = MemoryManager::AllocateObject(L"FastCgi.Request",  op_stack, *stack_pos, false);
    long* res_obj = MemoryManager::AllocateObject(L"FastCgi.Response", op_stack, *stack_pos, false);

    if(req_obj && res_obj) {
      req_obj[0] = (long)in;
      req_obj[1] = (long)envp;

      res_obj[0] = (long)out;
      res_obj[1] = (long)err;

      // set method calling parameters
      op_stack[0] = (long)req_obj;
      op_stack[1] = (long)res_obj;
      *stack_pos = 2;

      // execute method
      intpr.Execute((long*)op_stack, (long*)stack_pos, 0, mthd, NULL, false);
    }
    else {
      wcerr << L">>> DLL call: Unable to allocate FastCgi.Request or FastCgi.Response <<<" << endl;
      return 1;
    }

#ifdef _DEBUG
    wcout << L"# final stack: pos=" << (*stack_pos) << L" #" << endl;
    if((*stack_pos) > 0) {
      for(int i = 0; i < (*stack_pos); i++) {
        wcout << L"dump: value=" << (void*)(*stack_pos) << endl;
      }
    }
#endif

    // clean up
    delete[] op_stack;
    op_stack = NULL;

    delete stack_pos;
    stack_pos = NULL;

#ifdef _DEBUG
    PrintEnv(out, "Request environment", envp);
    PrintEnv(out, "Initial environment", environ);
#endif
  }

  return 0;
}

/******************************
* Dump enviroment variables
******************************/
void PrintEnv(FCGX_Stream* out, const char* label, char** envp)
{
  wcout << endl << BytesToUnicode(label) << endl;
  for(; *envp != NULL; envp++) {
    wcout << L"\t" << BytesToUnicode(*envp) << endl;
  }
}


/******************************
 * Load file into memory
 ******************************/
wstring IniManager::LoadFile(const wstring &fn) {
  char* buffer;

  string file(fn.begin(), fn.end());
  ifstream in(file.c_str(), ios_base::in | ios_base::binary | ios_base::ate);
  if (in.good()) {
    // get file size
    in.seekg(0, ios::end);
    size_t buffer_size = (size_t)in.tellg();
    in.seekg(0, ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    in.read(buffer, buffer_size);
    // close file
    in.close();
  }
  else {
    wcerr << L"Unable to read file: " << fn << endl;
    exit(1);
  }
  wstring out = BytesToUnicode(buffer);

  free(buffer);
  return out;
}

/******************************
 * Write file
 ******************************/
bool IniManager::WriteFile(const wstring &fn, const wstring &buffer) {
  string file(fn.begin(), fn.end());
  ofstream out(file.c_str(), ios_base::out | ios_base::binary);
  if (out.good()) {
    const string bytes = UnicodeToBytes(buffer);
    out.write(bytes.c_str(), bytes.size());
    // close file
    out.close();
    return true;
  }
  else {
    wcerr << L"Unable to write file: " << fn << endl;
    exit(1);
  }

  return false;
}

/******************************
 * Next parse token
 ******************************/
void IniManager::NextChar() {
  if (cur_pos < input.size()) {
    cur_char = input[cur_pos++];
    if (cur_pos < input.size()) {
      next_char = input[cur_pos];
    }
    else {
      next_char = L'\0';
    }
  }
  else {
    cur_char = next_char = L'\0';
  }
}

/******************************
 * Clear sections and names/values
 ******************************/
void IniManager::Clear() {
  map<const wstring, map<const wstring, wstring>*>::iterator iter;
  for (iter = section_map.begin(); iter != section_map.end(); ++iter) {
    map<const wstring, wstring>* value_map = iter->second;
    value_map->clear();
    // free map
    delete value_map;
    value_map = NULL;
  }

  if (!section_map.empty()) {
    section_map.clear();
  }
  cur_pos = 0;
}

/******************************
 * Serializes internal structures
 ******************************/
wstring IniManager::Serialize() {
  wstring out;
  // sections
  map<const wstring, map<const wstring, wstring>*>::iterator section_iter;
  for (section_iter = section_map.begin(); section_iter != section_map.end(); ++section_iter) {
    out += L"[";
    out += section_iter->first;
    out += L"]\r\n";
    // name/value pairs
    map<const wstring, wstring>::iterator value_iter;
    for (value_iter = section_iter->second->begin(); value_iter != section_iter->second->end(); ++value_iter) {
      out += value_iter->first;
      out += L"=";
      out += value_iter->second;
      out += L"\r\n";
    }
  }

  return out;
}

/******************************
 * Parses setions and name/value
 * pairs and loads internal
 * structures
 ******************************/
void IniManager::Deserialize() {
  map<const wstring, wstring>* value_map = NULL;

  NextChar();
  while (cur_char != L'\0') {
    // ignore white space
    while (cur_char == L' ' || cur_char == L'\t' || cur_char == L'\r' || cur_char == L'\n') {
      NextChar();
    }

    // parse section
    size_t start;
    if (cur_char == L'[') {
      start = cur_pos;
      while (cur_pos < input.size() && iswprint(cur_char) && cur_char != L']') {
        NextChar();
      }
      const wstring section = input.substr(start, cur_pos - start - 1);
      if (cur_char == L']') {
        NextChar();
      }
      value_map = new map<const wstring, wstring>;
      section_map.insert(pair<const wstring, map<const wstring, wstring>*>(section, value_map));
    }
    // comment
    else if (cur_char == L'#') {
      while (cur_pos < input.size() && cur_char != L'\r' && cur_char != L'\n') {
        NextChar();
      }
    }
    // key/value
    else if (iswalpha(cur_char)) {
      start = cur_pos - 1;
      while (cur_pos < input.size() && iswprint(cur_char) && cur_char != L'=') {
        NextChar();
      }
      const wstring key = input.substr(start, cur_pos - start - 1);
      NextChar();

      wstring value;
      start = cur_pos - 1;
      while (cur_pos < input.size() && iswprint(cur_char) && cur_char != L'\r' && cur_char != L'\n') {
        if (cur_char == L'\\') {
          switch (next_char) {
          case L'n':
            value += L'\n';
            NextChar();
            break;
          case L'r':
            value += L'\r';
            NextChar();
            break;
          default:
            value += L'\\';
            break;
          }
        }
        else {
          value += cur_char;
        }

        NextChar();
      }

      // add key/value pair
      if (value_map) {
        value_map->insert(pair<const wstring, wstring>(key, value));
      }
    }
  }
}

/******************************
 * Constructor/deconstructor
 ******************************/
IniManager::IniManager(const wstring &fn)
{
  filename = fn;
  cur_char = next_char = L'\0';
  cur_pos = 0;
  locked = false;

  Load();
}

IniManager::~IniManager() {
  Save();
  Clear();
}

/******************************
 * Fetch value per section and key
 ******************************/
wstring IniManager::GetValue(const wstring &sec, const wstring &key) {
  if(locked) {
    return L"";
  }
  
  locked = true;
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    map<const wstring, wstring>::iterator value = section->second->find(key);
    if (value != section->second->end()) {
      locked = false;
      return value->second;
    }
  }

  locked = false;
  return L"";
}

/******************************
 * Fetch value per section and key
 ******************************/
bool IniManager::SetValue(const wstring &sec, const wstring &key, const wstring &value) {
  if(locked) {
    return false;
  }

  locked = true;
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    (*section->second)[key] = value;
    locked = false;
    return true;
  }

  locked = false;
  return false;
}

vector<wstring> IniManager::GetListValues(const wstring &sec, const wstring &key)
{
  vector<wstring> values;  
  
  const wstring raw_value = GetValue(sec, key);
  Tokenize(raw_value, values, L";");

  return values;
}

bool IniManager::AddListValue(const wstring &sec, const wstring &key, const wstring &val)
{
  // put values into a set
  set<wstring> values_set;
  vector<wstring> list_values = GetListValues(sec, key);
  for(size_t i  = 0; i < list_values.size(); ++i) {
    const wstring value = list_values[i];
    if(value.size() > 0) {
      values_set.insert(value);
    }
  }

  // check for existing entry
  set<wstring>::iterator iter = values_set.find(val);
  if(iter != values_set.end()) {
    // found
    return false;
  }
  else {
    values_set.insert(val);
  }
  
  // rebuilt and save value
  wstring new_value;
  for(iter = values_set.begin(); iter != values_set.end(); ++iter) {
    new_value += *iter;
    new_value += L";";
  }
  
  return SetValue(sec, key, new_value);
}

bool IniManager::RemoveListValue(const wstring &sec, const wstring &key, const wstring &val)
{
  // put values into a set
  set<wstring> values_set;
  vector<wstring> list_values = GetListValues(sec, key);
  for(size_t i  = 0; i < list_values.size(); ++i) {
    const wstring value = list_values[i];
    if(value.size() > 0) {
      values_set.insert(value);
    }
  }
  
  // rebuilt and save value
  wstring new_value;
  for(set<wstring>::iterator iter = values_set.begin(); iter != values_set.end(); ++iter) {
    const wstring list_value = *iter;
    if(list_value != val) {
      new_value += list_value;
      new_value += L";";
    }
  }
  
  return SetValue(sec, key, new_value);
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
bool IniManager::Load() {
  if(locked) {
    return false;
  }
  
  locked = true;
  Clear();
  input = LoadFile(filename);
  if (input.size() > 0) {
    Deserialize();
    locked = false;
    return true;
  }
  
  locked = false;
  return false;
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
bool IniManager::Save() {
  if(locked) {
    return false;
  }
  
  locked = true;
  const wstring out = Serialize();
  if (out.size() > 0) {
    bool wrote = WriteFile(filename, out);
    locked = false;
    return wrote;
  }
  
  locked = false;
  return false;
}
