#include "utils.h"

//----------------------------------------------------------------------------
// ProjectManager
//----------------------------------------------------------------------------
ProjectManager::ProjectManager(const wstring &name, const wstring &fn)
{

}

ProjectManager::~ProjectManager()
{

}

//----------------------------------------------------------------------------
// IniManager
//----------------------------------------------------------------------------
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
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    map<const wstring, wstring>::iterator value = section->second->find(key);
    if (value != section->second->end()) {
      return value->second;
    }
  }

  return L"";
}

/******************************
 * Fetch value per section and key
 ******************************/
void IniManager::SetValue(const wstring &sec, const wstring &key, wstring &value) {
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    (*section->second)[key] = value;
  }
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
void IniManager::Load() {
  Clear();

  input = LoadFile(filename);
  if (input.size() > 0) {
    Deserialize();
  }
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
void IniManager::Save() {
  const wstring out = Serialize();
  if (out.size() > 0) {
    WriteFile(filename, out);
  }
}

// TODO: UI operations
void IniManager::ShowOptionsDialog(wxWindow* parent) {
  
}

void IniManager::ShowNewProjectDialog(wxWindow* parent) {
  
}

void IniManager::AddOpenedFile(const wxString &fn) {

}
