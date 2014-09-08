#include "utils.h"

//----------------------------------------------------------------------------
// ProjectManager
//----------------------------------------------------------------------------
ProjectManager::ProjectManager(wstring &project_file) 
{

}

ProjectManager::~ProjectManager()
{

}

//----------------------------------------------------------------------------
// InIManager
//----------------------------------------------------------------------------
/******************************
 * Load file into memory
 ******************************/
wstring InIManager::LoadFile(wstring filename) {
  char* buffer;

  string fn(filename.begin(), filename.end());
  ifstream in(fn.c_str(), ios_base::in | ios_base::binary | ios_base::ate);
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
    wcerr << L"Unable to read file: " << filename << endl;
    exit(1);
  }
  wstring out = BytesToUnicode(buffer);

  free(buffer);
  return out;
}

/******************************
 * Write file
 ******************************/
bool InIManager::WriteFile(const wstring &filename, const wstring &output) {
  string fn(filename.begin(), filename.end());
  ofstream out(fn.c_str(), ios_base::out | ios_base::binary);
  if (out.good()) {
    const string bytes = UnicodeToBytes(output);
    out.write(bytes.c_str(), bytes.size());
    // close file
    out.close();
    return true;
  }
  else {
    wcerr << L"Unable to write file: " << filename << endl;
    exit(1);
  }

  return false;
}

/******************************
 * Next parse token
 ******************************/
void InIManager::NextChar() {
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
void InIManager::Clear() {
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
wstring InIManager::Serialize() {
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
void InIManager::Deserialize() {
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
InIManager::InIManager(const wstring &f) {
  filename = f;
  cur_char = next_char = L'\0';
  cur_pos = 0;

  Read();
}

InIManager::~InIManager() {
  Write();
  Clear();
}

/******************************
 * Fetch value per section and key
 ******************************/
wstring InIManager::GetValue(const wstring &sec, const wstring &key) {
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
void InIManager::SetValue(const wstring &sec, const wstring &key, wstring &value) {
  map<const wstring, map<const wstring, wstring>*>::iterator section = section_map.find(sec);
  if (section != section_map.end()) {
    (*section->second)[key] = value;
  }
}

/******************************
 * Write contentes of memory
 * to file
 ******************************/
void InIManager::Read() {
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
void InIManager::Write() {
  const wstring output = Serialize();
  if (output.size() > 0) {
    WriteFile(filename, output);
  }
}

//----------------------------------------------------------------------------
// GlobalOptions
//----------------------------------------------------------------------------

BEGIN_EVENT_TABLE(GlobalOptions, wxDialog)
EVT_BUTTON(myID_DLG_OPTIONS_PATH, GlobalOptions::OnFilePath)
END_EVENT_TABLE()

void GlobalOptions::OnFilePath(wxCommandEvent& event)
{
  wxDirDialog dirDialog(NULL, _("Choose directory path"), "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
  if (dirDialog.ShowModal() == wxID_CANCEL) {
    return;
  }

  m_filePath = dirDialog.GetPath();
}

GlobalOptions::GlobalOptions(wxWindow* parent, InIManager* ini, long style) :
wxDialog(parent, wxID_ANY, wxT("General Settings"), wxDefaultPosition, wxDefaultSize, style | wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  m_iniManager = ini;

  // add controls
  SetSizeHints(wxDefaultSize, wxDefaultSize);
  wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer* bSizer3 = new wxBoxSizer(wxHORIZONTAL);

  wxStaticText* staticText4 = new wxStaticText(this, wxID_ANY, wxT("Objeck Path"), wxDefaultPosition, wxDefaultSize, 0);
  staticText4->Wrap(-1);
  bSizer3->Add(staticText4, 0, wxALL, 5);

  wxString path_string = m_iniManager->GetValue(wxT("Options"), wxT("path"));
  m_textCtrl4 = new wxTextCtrl(this, wxID_ANY, path_string, wxDefaultPosition, wxDefaultSize, 0);
  bSizer3->Add(m_textCtrl4, 1, wxALL, 5);

  m_pathButton = new wxButton(this, myID_DLG_OPTIONS_PATH, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  bSizer3->Add(m_pathButton, 0, wxALL, 5);

  bSizer1->Add(bSizer3, 0, wxEXPAND, 5);
  wxStaticBoxSizer* sbSizer1 = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("Editor")), wxVERTICAL);

  wxFlexGridSizer* fgSizer1;
  fgSizer1 = new wxFlexGridSizer(3, 2, 0, 0);
  fgSizer1->SetFlexibleDirection(wxBOTH);
  fgSizer1->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

  wxStaticText* staticText6 = new wxStaticText(this, wxID_ANY, wxT("Line Endings"), wxDefaultPosition, wxDefaultSize, 0);
  staticText6->Wrap(-1);
  fgSizer1->Add(staticText6, 0, wxALL, 5);

  wxBoxSizer* bSizer6 = new wxBoxSizer(wxHORIZONTAL);

  m_winEnding = new wxRadioButton(this, wxID_ANY, wxT("Windows"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  bSizer6->Add(m_winEnding, 0, wxALL, 5);

  m_unixEnding = new wxRadioButton(this, wxID_ANY, wxT("Unix"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer6->Add(m_unixEnding, 0, wxALL, 5);

  m_macEndig = new wxRadioButton(this, wxID_ANY, wxT("Mac"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer6->Add(m_macEndig, 0, wxALL, 5);

  fgSizer1->Add(bSizer6, 1, wxEXPAND | wxLEFT, 5);

  wxStaticText* staticText8 = new wxStaticText(this, wxID_ANY, wxT("Indent"), wxDefaultPosition, wxDefaultSize, 0);
  staticText8->Wrap(-1);
  fgSizer1->Add(staticText8, 0, wxALL, 5);

  wxBoxSizer* bSizer7 = new wxBoxSizer(wxHORIZONTAL);

  m_tabIdent = new wxRadioButton(this, wxID_ANY, wxT("Tab"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  bSizer7->Add(m_tabIdent, 0, wxALL, 5);

  m_spaceIdent = new wxRadioButton(this, wxID_ANY, wxT("Spaces"), wxDefaultPosition, wxDefaultSize, 0);
  bSizer7->Add(m_spaceIdent, 0, wxALL, 5);

  m_identSize = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(50, -1), wxSP_ARROW_KEYS, 0, 10, 0);
  bSizer7->Add(m_identSize, 0, wxALL, 5);

  fgSizer1->Add(bSizer7, 1, wxEXPAND | wxLEFT, 5);

  m_fontSelect = new wxStaticText(this, wxID_ANY, wxT("Font"), wxDefaultPosition, wxDefaultSize, 0);
  m_fontSelect->Wrap(-1);
  fgSizer1->Add(m_fontSelect, 0, wxALL, 5);

  wxBoxSizer* bSizer8 = new wxBoxSizer(wxHORIZONTAL);

  m_comboBox1 = new wxComboBox(this, wxID_ANY, wxT("Combo!"), wxDefaultPosition, wxDefaultSize, 0, NULL, 0);
  bSizer8->Add(m_comboBox1, 0, wxALL, 5);

  wxStaticText* staticText10 = new wxStaticText(this, wxID_ANY, wxT("Size"), wxDefaultPosition, wxDefaultSize, 0);
  staticText10->Wrap(-1);
  bSizer8->Add(staticText10, 0, wxALL, 5);

  font_size = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 10, 0);
  font_size->SetMinSize(wxSize(50, -1));

  bSizer8->Add(font_size, 0, wxALL, 5);
  fgSizer1->Add(bSizer8, 1, wxEXPAND, 5);
  sbSizer1->Add(fgSizer1, 1, wxEXPAND, 5);
  bSizer1->Add(sbSizer1, 1, wxEXPAND, 5);

  m_sdbSizer1 = new wxStdDialogButtonSizer();
  m_sdbSizer1OK = new wxButton(this, wxID_OK);
  m_sdbSizer1->AddButton(m_sdbSizer1OK);
  m_sdbSizer1Cancel = new wxButton(this, wxID_CANCEL);
  m_sdbSizer1->AddButton(m_sdbSizer1Cancel);
  m_sdbSizer1->Realize();

  bSizer1->Add(m_sdbSizer1, 1, wxEXPAND, 5);

  SetSizer(bSizer1);
  Layout();

  Centre(wxBOTH);
}

void GlobalOptions::ShowSave() {
  // write out values
  if (ShowModal() == wxID_OK) {
    // save values
    wstring path_string = m_textCtrl4->GetValue().ToStdWstring();
    m_iniManager->SetValue(wxT("Options"), wxT("path"), path_string);
    // write out
    m_iniManager->Write();
  }
}
