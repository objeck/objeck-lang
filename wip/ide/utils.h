#ifndef __UTILS_H__
#define __UTILS_H__

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#include <string>
#include <vector>
#include <map>
#include <fstream>

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/combobox.h>
#include <wx/statbox.h>
#include <wx/dialog.h>
#include <wx/artprov.h>
#include <wx/wxhtml.h>
#include <wx/utils.h>
#include <wx/process.h>

#include "../../src/shared/sys.h"
#include "defsext.h"

using namespace std;

//----------------------------------------------------------------------------
//! InIManager
class ProjectManager {
  wstring name;
  vector<wstring> file_names;
  vector<wstring> lib_names;
  
 public:
  ProjectManager(wstring &project_file);
  ~ProjectManager();
};

//----------------------------------------------------------------------------
//! InIManager
class InIManager {
  map<const wstring, map<const wstring, wstring>*> section_map;
  wstring filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;

  wstring LoadFile(wstring filename);
  bool WriteFile(const wstring &filename, const wstring &output);
  void NextChar();
  void Clear();
  wstring Serialize();
  void Deserialize();

public:
  InIManager(const wstring &f);
  ~InIManager();

  wstring GetValue(const wstring &sec, const wstring &key);
  void SetValue(const wstring &sec, const wstring &key, wstring &value);
  void Read();
  void Write();
};

//----------------------------------------------------------------------------
//! EditProperties
class GlobalOptions : public wxDialog {
  wxStaticText* m_fontSelect;
  wxTextCtrl* m_textCtrl4;
  wxButton* m_pathButton;
  wxRadioButton* m_winEnding;
  wxRadioButton* m_unixEnding;
  wxRadioButton* m_macEndig;
  wxRadioButton* m_tabIdent;
  wxRadioButton* m_spaceIdent;
  wxSpinCtrl* m_identSize;
  wxComboBox* m_comboBox1;
  wxSpinCtrl* font_size;
  wxStdDialogButtonSizer* m_sdbSizer1;
  wxButton* m_sdbSizer1OK;
  wxButton* m_sdbSizer1Cancel;
  InIManager* m_iniManager;
  wxString m_filePath;

  void OnFilePath(wxCommandEvent& event);

public:
  //! constructor
  GlobalOptions(wxWindow* parent, InIManager* ini, long style = 0);

  wxString GetPath() {
    return m_filePath;
  }

  void ShowSave();

  DECLARE_EVENT_TABLE()
};

#endif
