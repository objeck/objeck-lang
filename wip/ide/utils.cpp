#include "utils.h"
#include "dialogs.h"
#include "ide.h"

#include <wx/tokenzr.h>

//----------------------------------------------------------------------------
// IniManager
//----------------------------------------------------------------------------
/******************************
 * Load file into memory
 ******************************/
wxString IniManager::LoadFile(const wxString &fn) {
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
  wxString out = BytesToUnicode(buffer);

  free(buffer);
  return out;
}

/******************************
 * Write file
 ******************************/
bool IniManager::WriteFile(const wxString &fn, const wxString &buffer) {
  string file(fn.begin(), fn.end());
  ofstream out(file.c_str(), ios_base::out | ios_base::binary);
  if (out.good()) {
    const string bytes = UnicodeToBytes(buffer.ToStdWstring());
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
  map<const wxString, map<const wxString, wxString>*>::iterator iter;
  for (iter = section_map.begin(); iter != section_map.end(); ++iter) {
    map<const wxString, wxString>* value_map = iter->second;
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
wxString IniManager::Serialize() {
  wxString out;
  // sections
  map<const wxString, map<const wxString, wxString>*>::iterator section_iter;
  for (section_iter = section_map.begin(); section_iter != section_map.end(); ++section_iter) {
    out += L"[";
    out += section_iter->first;
    out += L"]\r\n";
    // name/value pairs
    map<const wxString, wxString>::iterator value_iter;
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
  map<const wxString, wxString>* value_map = NULL;

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
      const wxString section = input.substr(start, cur_pos - start - 1);
      if (cur_char == L']') {
        NextChar();
      }
      value_map = new map<const wxString, wxString>;
      section_map.insert(pair<const wxString, map<const wxString, wxString>*>(section, value_map));
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
      const wxString key = input.substr(start, cur_pos - start - 1);
      NextChar();

      wxString value;
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
        value_map->insert(pair<const wxString, wxString>(key, value));
      }
    }
  }
}

/******************************
 * Constructor/deconstructor
 ******************************/
IniManager::IniManager(const wxString &fn)
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
wxString IniManager::GetValue(const wxString &sec, const wxString &key) {
  if(locked) {
    return L"";
  }
  
  locked = true;
  map<const wxString, map<const wxString, wxString>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    map<const wxString, wxString>::iterator value = section->second->find(key);
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
bool IniManager::SetValue(const wxString &sec, const wxString &key, const wxString &value) {
  if(locked) {
    return false;
  }

  locked = true;
  map<const wxString, map<const wxString, wxString>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    (*section->second)[key] = value;
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
  const wxString out = Serialize();
  if (out.size() > 0) {
    bool wrote = WriteFile(filename, out);
    locked = false;
    return wrote;
  }
  
  locked = false;
  return false;
}

// TODO: UI operations
void IniManager::ShowOptionsDialog(wxWindow* parent) 
{
  // load and read values
  Load();
  const wxString objeck_path(GetValue(L"Options", L"objeck_path"));
  const wxString indent_spacing(GetValue(L"Options", L"indent_spacing"));
  const wxString line_ending(GetValue(L"Options", L"line_ending"));
  
  // show dialog
  GeneralOptions options(parent, this, objeck_path, indent_spacing, line_ending);
  options.ShowAndUpdate();
}

//----------------------------------------------------------------------------
// ProjectManager
//----------------------------------------------------------------------------
ProjectManager::ProjectManager(MyFrame* parent, const wxString &name, const wxString &filename)
{
	wxString project_string = wxT("name=" + name + "\r\n");
	project_string += wxT("source=\r\n");
	project_string += wxT("libraries=\r\n");

	IniManager::WriteFile(filename, project_string);
  iniManager = new IniManager(filename);
}

ProjectManager::ProjectManager(MyFrame* parent, const wxString &filename)
{
  iniManager = new IniManager(filename);
}

ProjectManager::~ProjectManager()
{
  delete iniManager;
}

bool ProjectManager::AddFile(const wxString &filename)
{

}
 
bool ProjectManager::RemoveFile(const wxString &filename)
{

}

wxArrayString ProjectManager::GetFiles() 
{
  wxArrayString source_files;
  
  const wxString source_string = iniManager->GetValue(L"Project", L"source");
  wxStringTokenizer tokenizer(source_string, L";");
  while (tokenizer.HasMoreTokens()) {
    const wxString source = tokenizer.GetNextToken();
    source_files.Add(source);
  }

  return source_files;
}

bool ProjectManager::AddLibrary(const wxString &name)
{

}

bool ProjectManager::RemoveLibrary(const wxString &name)
{

}

bool ProjectManager::Compile()
{

}
